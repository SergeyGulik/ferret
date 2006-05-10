#include "ferret.h"
#include "store.h"

VALUE cLock;
VALUE cDirectory;
VALUE cRAMDirectory;
VALUE cFSDirectory;


static ID id_mkdir_p;
static ID id_is_directory;

/****************************************************************************
 *
 * Lock Methods
 *
 ****************************************************************************/

void
frt_lock_free(void *p)
{
  Lock *lock = (Lock *)p;
  if (RTEST(object_get(lock->store))) {
    lock->store->close_lock(lock);
  } else {
    free(lock->name);
    free(lock);
  }
}

void
frt_lock_mark(void *p)
{
  Lock *lock = (Lock *)p;
  rb_gc_mark(object_get(lock->store));
}

#define GET_LOCK Lock *lock; Data_Get_Struct(self, Lock, lock);
static VALUE
frt_lock_obtain(int argc, VALUE *argv, VALUE self)
{
  VALUE rtimeout;
  int timeout = 1;
  GET_LOCK;
  if (rb_scan_args(argc, argv, "01", &rtimeout) > 0) {
    timeout = FIX2INT(rtimeout);
  }
  /* TODO: use the lock timeout */
  if (!lock->obtain(lock)) {
    rb_raise(rb_eStandardError, "could not obtain lock: #%s", lock->name);
  }
  return Qtrue;
}

static VALUE
frt_lock_while_locked(int argc, VALUE *argv, VALUE self)
{
  VALUE rtimeout;
  int timeout = 1;
  GET_LOCK;
  if (rb_scan_args(argc, argv, "01", &rtimeout) > 0) {
    timeout = FIX2INT(rtimeout);
  }
  if (!lock->obtain(lock)) {
    rb_raise(rb_eStandardError, "could not obtain lock: #%s", lock->name);
  }
  rb_yield(Qnil);
  lock->release(lock);
  return Qtrue;
}

static VALUE
frt_lock_is_locked(VALUE self)
{
  GET_LOCK;
  return lock->is_locked(lock) ? Qtrue : Qfalse;
}

static VALUE
frt_lock_release(VALUE self)
{
  GET_LOCK;
  lock->release(lock);
  return Qnil;
}

/****************************************************************************
 *
 * Directory Methods
 *
 ****************************************************************************/

void
frt_dir_free(Store *store)
{
  object_del(store);
  store_deref(store);
}

#define GET_STORE Store *store; Data_Get_Struct(self, Store, store)
static VALUE
frt_dir_close(VALUE self)
{
  /*
   * No need to do anything here. Leave it do the garbage collector
  GET_STORE;
  Frt_Unwrap_Struct(self);
  object_del(store);
  store_deref(store);
  */
  return Qnil;
}

static VALUE
frt_dir_exists(VALUE self, VALUE rfname)
{
  GET_STORE;
  rfname = rb_obj_as_string(rfname);
  return store->exists(store, RSTRING(rfname)->ptr) ? Qtrue : Qfalse;
}

static VALUE
frt_dir_touch(VALUE self, VALUE rfname)
{
  GET_STORE;
  rfname = rb_obj_as_string(rfname);
  store->touch(store, RSTRING(rfname)->ptr);
  return Qnil;
}

typedef struct RTerm {
  VALUE field;
  VALUE text;
} RTerm;

static VALUE
frt_dir_delete(VALUE self, VALUE rfname)
{
  GET_STORE;
  rfname = rb_obj_as_string(rfname);
  return INT2FIX(store->remove(store, RSTRING(rfname)->ptr));
}

static VALUE
frt_dir_file_count(VALUE self)
{
  GET_STORE;
  return INT2FIX(store->count(store));
}

static VALUE
frt_dir_refresh(VALUE self)
{
  GET_STORE;
  store->clear_all(store);
  return Qnil;
}

static VALUE
frt_dir_rename(VALUE self, VALUE rfrom, VALUE rto)
{
  GET_STORE;
  rfrom = rb_obj_as_string(rfrom);
  rto = rb_obj_as_string(rto);
  store->rename(store, RSTRING(rfrom)->ptr, RSTRING(rto)->ptr);
  return Qnil;
}

static VALUE
frt_dir_make_lock(VALUE self, VALUE rlock_name)
{
  GET_STORE;
  rlock_name = rb_obj_as_string(rlock_name);
  return Data_Wrap_Struct(cLock, &frt_lock_mark, &frt_lock_free,
      store->open_lock(store, RSTRING(rlock_name)->ptr));
}

/****************************************************************************
 *
 * RAMDirectory Methods
 *
 ****************************************************************************/

static VALUE
frt_ramdir_init(int argc, VALUE *argv, VALUE self) 
{
  VALUE rdir, rclose_dir;
  Store *store;
  bool close_dir = false;
  switch (rb_scan_args(argc, argv, "02", &rdir, &rclose_dir)) {
    case 2: close_dir = RTEST(rclose_dir);
    case 1: {
              Store *ostore;
              Data_Get_Struct(rdir, Store, ostore);
              if (close_dir) Frt_Unwrap_Struct(rdir);
              store = open_ram_store_and_copy(ostore, close_dir);
              break;
            }
    default: store = open_ram_store();
  }
  Frt_Wrap_Struct(self, NULL, frt_dir_free, store);
  object_add(store, self);
  return self;
}

/****************************************************************************
 *
 * FSDirectory Methods
 *
 ****************************************************************************/

static VALUE
frt_fsdir_new(VALUE klass, VALUE rpath, VALUE rcreate) 
{
  VALUE self;
  Store *store;
  bool create = RTEST(rcreate);
  rpath = rb_obj_as_string(rpath);
  if (create) {
    VALUE mFileUtils;
    rb_require("fileutils");
    mFileUtils = rb_define_module("FileUtils");
    rb_funcall(mFileUtils, id_mkdir_p, 1, rpath);
  }
  if (!rb_funcall(rb_cFile, id_is_directory, 1, rpath)) {
    rb_raise(rb_eIOError, "There is no directory: %s. Use create = true to "
        "create one.", RSTRING(rpath)->ptr);
  }
  store = open_fs_store(RSTRING(rpath)->ptr);
  if (create) store->clear_all(store);
  if ((self = object_get(store)) == Qnil) {
    self = Data_Wrap_Struct(klass, NULL, &frt_dir_free, store);
    object_add(store, self);
  } else {
    store_deref(store);
  }
  return self;
}

/****************************************************************************
 *
 * Init Function
 *
 ****************************************************************************/

void
Init_dir(void)
{
  id_mkdir_p = rb_intern("mkdir_p");
  id_is_directory = rb_intern("directory?");

  cLock = rb_define_class_under(mStore, "Lock", rb_cObject);
  rb_define_method(cLock, "obtain", frt_lock_obtain, -1);
  rb_define_method(cLock, "while_locked", frt_lock_while_locked, -1);
  rb_define_method(cLock, "release", frt_lock_release, 0);
  rb_define_method(cLock, "locked?", frt_lock_is_locked, 0);

  cDirectory = rb_define_class_under(mStore, "Directory", rb_cObject);
  rb_define_const(cDirectory, "LOCK_PREFIX", rb_str_new2(LOCK_PREFIX));
  rb_define_method(cDirectory, "close", frt_dir_close, 0);\
  rb_define_method(cDirectory, "exists?", frt_dir_exists, 1);\
  rb_define_method(cDirectory, "touch", frt_dir_touch, 1);\
  rb_define_method(cDirectory, "delete", frt_dir_delete, 1);\
  rb_define_method(cDirectory, "file_count", frt_dir_file_count, 0);\
  rb_define_method(cDirectory, "refresh", frt_dir_refresh, 0);\
  rb_define_method(cDirectory, "rename", frt_dir_rename, 2);\
  rb_define_method(cDirectory, "make_lock", frt_dir_make_lock, 1);

  /* RAMDirectory */
  cRAMDirectory = rb_define_class_under(mStore, "RAMDirectory", cDirectory);
  rb_define_alloc_func(cRAMDirectory, frt_data_alloc);
  rb_define_method(cRAMDirectory, "initialize", frt_ramdir_init, -1);

  /* FSDirectory */
  cFSDirectory = rb_define_class_under(mStore, "FSDirectory", cDirectory);
  rb_define_alloc_func(cFSDirectory, frt_data_alloc);
  rb_define_singleton_method(cFSDirectory, "new", frt_fsdir_new, 2);
}
