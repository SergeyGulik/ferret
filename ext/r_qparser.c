#include "ferret.h"
#include "search.h"

static VALUE cQueryParser;
VALUE cQueryParseException;

VALUE rhandle_parse_errors_key;
VALUE rallow_any_fields_key;
VALUE rwild_lower_key;
VALUE roccur_default_key;
VALUE rdefault_slop_key;
VALUE rclean_str_key;
VALUE rfields_key;
extern VALUE ranalyzer_key;

extern VALUE frt_get_analyzer(Analyzer *a);
extern VALUE frt_get_q(Query *q);
extern Analyzer *frt_get_cwrapped_analyzer(VALUE ranalyzer);

/****************************************************************************
 *
 * QueryParser Methods
 *
 ****************************************************************************/

static void
frt_qp_free(void *p)
{
  QParser *qp = (QParser *)p;
  object_del(p);
  qp_destroy(qp);
}

static void
frt_qp_mark(void *p)
{
  QParser *qp = (QParser *)p;
  frt_gc_mark(qp->analyzer);
}

HashSet *
frt_get_fields(VALUE rfields)
{
  VALUE rval;
  HashSet *fields = hs_str_create(&free);
  char *s, *p, *str;

  if (TYPE(rfields) == T_ARRAY) {
    int i;
    for (i = 0; i < RARRAY(rfields)->len; i++) {
      rval = rb_obj_as_string(RARRAY(rfields)->ptr[i]);
      hs_add(fields, estrdup(RSTRING(rval)->ptr));
    }
  } else {
    rval = rb_obj_as_string(rfields);
    if (strcmp("*", RSTRING(rval)->ptr) == 0) {
      hs_destroy(fields);
      fields = NULL;
    } else {
      s = str = estrdup(RSTRING(rval)->ptr);
      while ((p = strchr(s, '|')) != '\0') {
        *p = '\0';
        hs_add(fields, estrdup(s));
        s = p + 1;
      }
      hs_add(fields, estrdup(s));
      free(str);
    }
  }
  return fields;
}

static VALUE
frt_qp_init(int argc, VALUE *argv, VALUE self)
{
  VALUE rdef_field, roptions;
  VALUE rval;
  Analyzer *analyzer = NULL;

  HashSet *all_fields = hs_str_create(&free);
  HashSet *def_fields = NULL;
  QParser *qp;

  rb_scan_args(argc, argv, "02", &rdef_field, &roptions);
  /* process default_field */
  if (argc > 0) {
    def_fields = frt_get_fields(rdef_field);
  }

  if (argc == 2) {
    if (Qnil != (rval = rb_hash_aref(roptions, ranalyzer_key))) {
      analyzer = frt_get_cwrapped_analyzer(rval);
    }
    if (Qnil != (rval = rb_hash_aref(roptions, rfields_key))) {
      all_fields = frt_get_fields(rval);
    }
  }
  if (all_fields == NULL) {
    all_fields = hs_str_create(&free);
  }

  if (!analyzer) {
    analyzer = mb_standard_analyzer_create(true);
  }

  qp = qp_create(all_fields, def_fields, analyzer);
  qp->allow_any_fields = true;
  qp->clean_str = true;
  /* handle options */
  if (argc == 2) {
    if (Qnil != (rval = rb_hash_aref(roptions, rhandle_parse_errors_key))) {
      qp->handle_parse_errors = RTEST(rval);
    }
    if (Qnil != (rval = rb_hash_aref(roptions, rallow_any_fields_key))) {
      qp->allow_any_fields = RTEST(rval);
    }
    if (Qnil != (rval = rb_hash_aref(roptions, rwild_lower_key))) {
      qp->wild_lower = RTEST(rval);
    }
    if (Qnil != (rval = rb_hash_aref(roptions, roccur_default_key))) {
      qp->or_default = (FIX2INT(rval) == BC_MUST) ? false : true;
    }
    if (Qnil != (rval = rb_hash_aref(roptions, rdefault_slop_key))) {
      qp->def_slop = FIX2INT(rval);
    }
    if (Qnil != (rval = rb_hash_aref(roptions, rclean_str_key))) {
      qp->clean_str = RTEST(rval);
    }
  }
  Frt_Wrap_Struct(self, frt_qp_mark, frt_qp_free, qp);
  object_add(qp, self);
  return self;
}

#define GET_QP QParser *qp = (QParser *)DATA_PTR(self)
static VALUE
frt_qp_parse(VALUE self, VALUE rstr)
{
  char *msg = NULL;
  volatile VALUE rq;
  GET_QP;
  rstr = rb_obj_as_string(rstr);
  TRY
    rq = frt_get_q(qp_parse(qp, RSTRING(rstr)->ptr));
    break;
  default:
    msg = xcontext.msg;
    HANDLED();
  XENDTRY

  if (msg) {
    rb_raise(cQueryParseException, msg);
  }
  
  return rq;
}

static VALUE
frt_qp_get_fields(VALUE self)
{
  GET_QP;
  int i;
  HashSet *fields = qp->all_fields;
  VALUE rfields = rb_ary_new();

  for (i = 0; i < fields->size; i++) {
    rb_ary_push(rfields, rb_str_new2((char *)fields->elems[i]));
  } 

  return rfields;
}

static VALUE
frt_qp_set_fields(VALUE self, VALUE rfields)
{
  GET_QP;
  HashSet *fields = frt_get_fields(rfields);

  if (fields == NULL) {
    fields = hs_str_create(&free);
  }
  hs_destroy(qp->all_fields);
  qp->all_fields = fields;

  return self;
}

/****************************************************************************
 *
 * Init function
 *
 ****************************************************************************/

void
Init_qparser(void)
{
  /* hash keys */
  rhandle_parse_errors_key = ID2SYM(rb_intern("handle_parse_errors"));
  rallow_any_fields_key = ID2SYM(rb_intern("allow_any_fields"));
  rwild_lower_key = ID2SYM(rb_intern("wild_lower"));
  roccur_default_key = ID2SYM(rb_intern("occur_default"));
  rdefault_slop_key = ID2SYM(rb_intern("default_slop"));
  rclean_str_key = ID2SYM(rb_intern("clean_string"));
  rfields_key = ID2SYM(rb_intern("fields"));

  /* QueryParser */
  cQueryParser = rb_define_class_under(mFerret, "QueryParser", rb_cObject);
  rb_define_alloc_func(cQueryParser, frt_data_alloc);

  rb_define_method(cQueryParser, "initialize", frt_qp_init, -1);
  rb_define_method(cQueryParser, "parse", frt_qp_parse, 1);
  rb_define_method(cQueryParser, "fields", frt_qp_get_fields, 0);
  rb_define_method(cQueryParser, "fields=", frt_qp_set_fields, 1);

  /* QueryParseException */
  cQueryParseException = rb_define_class_under(cQueryParser,
      "QueryParseException", rb_eStandardError);
}
