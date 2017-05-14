#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include "ruby.h"
#include "cdb/alloc.h"
#include "cdb/uint32.h"
#include "cdb/cdb.h"
#include "cdb/cdb_make.h"

#define Get_CDB(obj, var) Data_Get_Struct(obj, struct cdb, var)
#define Get_CDBMake(obj, var) Data_Get_Struct(obj, struct cdb_make, var)

#define Check_FD(cdb) if (cdb->fd == -1) rb_raise(rb_eCDB_Error, "file already closed")

VALUE rb_eCDB_Error;



static void
_xread (fd, buf, len)
   int fd;
   char *buf;
   unsigned int len;
{
   int ret;
   while (len > 0) {
      do
         ret = read(fd, buf, len);
      while ((ret == -1) && (errno == EINTR));
      if (ret == -1) rb_sys_fail(0);
      if (ret == 0) rb_raise(rb_eCDB_Error, "data format error");
      len -= ret;
      buf += ret;
   }
}


/*
 * class CDB
 */

VALUE rb_cCDB;

static void
_cdb_free (cdb)
   struct cdb *cdb;
{
   cdb_free(cdb);
   if (cdb->fd != -1) close(cdb->fd);
   free(cdb);
}


static VALUE
rb_cdb_new (klass, file)
   VALUE klass, file;
{
   int fd;
   VALUE obj;
   struct cdb *cdb;

   Check_Type(file, T_STRING);

   fd = open(StringValuePtr(file), O_RDONLY, 0);
   if (fd == -1) rb_sys_fail(0);

   obj = Data_Make_Struct(klass, struct cdb, 0, _cdb_free, cdb);
   cdb_init(cdb, fd);

   return obj;
}


static VALUE
rb_cdb_close (obj)
   VALUE obj;
{
   struct cdb *cdb;
   Get_CDB(obj, cdb);
   Check_FD(cdb);
   cdb_free(cdb);
   close(cdb->fd);
   cdb->fd = -1;
   return Qnil;
}


static VALUE
rb_cdb_open (klass, file)
   VALUE klass, file;
{
   VALUE obj;
   obj = rb_cdb_new(klass, file);
   return rb_ensure(rb_yield, obj, rb_cdb_close, obj);
}


static VALUE
_cdb_read (cdb, pos, len)
   struct cdb *cdb;
   uint32 pos;
   unsigned int len;
{
   if (cdb->map) {
      if ((pos > cdb->size) || (cdb->size - pos < len))
         rb_raise(rb_eCDB_Error, "data format error");
      return rb_str_new(cdb->map + pos, len);
   }
   else {
      int ret;
      VALUE s;

      if (seek_set(cdb->fd, pos) == -1) rb_sys_fail(0);
      s = rb_str_new(0, len);
      _xread(cdb->fd, RSTRING_PTR(s), len);
      return s;
   }
}


static VALUE
rb_cdb_read (obj, p, l)
   VALUE obj, p, l;
{
   struct cdb *cdb;
   uint32 pos = NUM2INT(p);
   unsigned int len = NUM2INT(l);

   Get_CDB(obj, cdb);
   Check_FD(cdb);
   return _cdb_read(cdb, pos, len);
}


static VALUE
rb_cdb_find (obj, key)
   VALUE obj, key;
{
   struct cdb *cdb;
   uint32 pos;

   Get_CDB(obj, cdb);
   Check_FD(cdb);
   StringValue(key);
   switch (cdb_find(cdb, RSTRING_PTR(key), RSTRING_LEN(key))) {
      case 1:
         return _cdb_read(cdb, cdb_datapos(cdb), cdb_datalen(cdb));
      case 0:
         return Qnil;
      case -1:
         rb_sys_fail(0);
      default:
         rb_raise(rb_eCDB_Error, "cdb_find returned unexpected value");
   }
}


static void
_read_uint32 (fd, num)
   int fd;
   uint32 *num;
{
   char buf[4];
   _xread(fd, buf, 4);
   uint32_unpack(buf, num);
}


static VALUE
rb_cdb_each (argc, argv, obj)
   int argc;
   VALUE *argv, obj;
{
   int ret;
   VALUE key;
   struct cdb *cdb;

   Get_CDB(obj, cdb);
   Check_FD(cdb);

   if (rb_scan_args(argc, argv, "01", &key) == 1) {
      StringValue(key);
      cdb_findstart(cdb);

      while ((ret = cdb_findnext(cdb, RSTRING_PTR(key), RSTRING_LEN(key))) == 1)
         rb_yield(_cdb_read(cdb, cdb_datapos(cdb), cdb_datalen(cdb)));

      switch (ret) {
         case 0: return obj; 
         case -1: rb_sys_fail(0);
         default: rb_raise(rb_eCDB_Error, "cdb_find returned unexpected value");
      }
   }
   else {
      int fd;
      VALUE data;
      struct stat sb;
      uint32 eod, klen, dlen, pos;

      fd = cdb->fd;
      if (fstat(fd, &sb) == -1) rb_sys_fail(0);
      if (cdb->map) {
         char *eodp, *p = cdb->map;

         uint32_unpack(p, &eod);
         if (sb.st_size < eod)
            rb_raise(rb_eCDB_Error, "data format error");
         eodp = p + eod;
         p += 2048;

         while (p < eodp) {
            uint32_unpack(p, &klen); p += 4;
            uint32_unpack(p, &dlen); p += 4;
            rb_yield(rb_assoc_new(rb_str_new(p, klen),
                                  rb_str_new(p + klen, dlen)));
            p += klen + dlen;
         }
      }
      else {
         if (seek_set(fd, 0) == -1) rb_sys_fail(0);
         _read_uint32(fd, &eod);
         if (sb.st_size < eod)
            rb_raise(rb_eCDB_Error, "data format error");
 
         if (seek_set(fd, 2048) == -1) rb_sys_fail(0);
         pos = 2048;

         while (pos < eod) {
            _read_uint32(fd, &klen);
            _read_uint32(fd, &dlen);

            key = rb_str_new(0, klen);
            data = rb_str_new(0, dlen);
            _xread(fd, RSTRING_PTR(key), klen);
            _xread(fd, RSTRING_PTR(key), dlen);

            rb_yield(rb_assoc_new(key, data));
            pos += 8 + klen + dlen;
         }
      }
      return obj;
   }
}



/*
 * class CDBMake 
 */

VALUE rb_cCDBMake;

static void
_cdbmake_free (cdb)
   struct cdb_make *cdb;
{
   struct cdb_hplist *head, *next;
   head = cdb->head;
   while (head) {
      next = head->next;
      alloc_free(head);
      head = next;
   }
   if (cdb->split) alloc_free(cdb->split);
   if (cdb->fd != -1) close(cdb->fd);
   free(cdb);
}


static VALUE
rb_cdbmake_new (argc, argv, klass)
   int argc;
   VALUE *argv, klass;
{
   int fd;
   mode_t mode = 0644;
   VALUE obj, arg1, arg2;
   struct cdb_make *cdb;

   if (rb_scan_args(argc, argv, "11", &arg1, &arg2) == 2)
      mode = NUM2INT(arg2);

   Check_Type(arg1, T_STRING);

   fd = open(StringValuePtr(arg1), O_WRONLY|O_CREAT|O_TRUNC, mode);
   if (fd == -1) rb_sys_fail(0);

   obj = Data_Make_Struct(klass, struct cdb_make, 0, _cdbmake_free, cdb);
   if (cdb_make_start(cdb, fd) == -1) rb_sys_fail(0);

   return obj;
}


static VALUE
rb_cdbmake_finish (obj)
   VALUE obj;
{
   struct cdb_make *cdb;

   Get_CDBMake(obj, cdb);
   Check_FD(cdb);
   if (cdb_make_finish(cdb) == -1) rb_sys_fail(0);
   if (fsync(cdb->fd) == -1) rb_sys_fail(0);
   close(cdb->fd);
   cdb->fd = -1;
   return Qnil;
}


static VALUE
rb_cdbmake_open (argc, argv, klass)
   int argc;
   VALUE *argv, klass;
{
   VALUE obj;
   obj = rb_cdbmake_new(argc, argv, klass);
   return rb_ensure(rb_yield, obj, rb_cdbmake_finish, obj);
}


static VALUE
rb_cdbmake_add (obj, key, data)
   VALUE obj, key, data;
{
   struct cdb_make *cdb;
   
   Get_CDBMake(obj, cdb);
   Check_FD(cdb);
   StringValue(key);
   StringValue(data);
   if (cdb_make_add(cdb, RSTRING_PTR(key), RSTRING_LEN(key),
                         RSTRING_PTR(data), RSTRING_LEN(data)) == -1) rb_sys_fail(0);
   return obj;
}



/*
 * Module Initialization
 */
 
void
Init_cdb ()
{
   rb_cCDB = rb_define_class("CDB", rb_cObject);
   rb_cCDBMake = rb_define_class("CDBMake", rb_cObject);
   rb_eCDB_Error = rb_define_class("CDBError", rb_eRuntimeError);

   rb_include_module(rb_cCDB, rb_mEnumerable);

   rb_define_singleton_method(rb_cCDB, "new", rb_cdb_new, 1);
   rb_define_singleton_method(rb_cCDB, "open", rb_cdb_open, 1);

   rb_define_singleton_method(rb_cCDBMake, "new", rb_cdbmake_new, -1);
   rb_define_singleton_method(rb_cCDBMake, "open", rb_cdbmake_open, -1);

   rb_define_method(rb_cCDB, "find", rb_cdb_find, 1);
   rb_define_method(rb_cCDB, "[]", rb_cdb_find, 1);
   rb_define_method(rb_cCDB, "has", rb_cdb_find, 1);
   rb_define_method(rb_cCDB, "each", rb_cdb_each, -1);
   rb_define_method(rb_cCDB, "read", rb_cdb_read, 2);
   rb_define_method(rb_cCDB, "close", rb_cdb_close, 0);

   rb_define_method(rb_cCDBMake, "add", rb_cdbmake_add, 2);
   rb_define_method(rb_cCDBMake, "[]=", rb_cdbmake_add, 2);
   rb_define_method(rb_cCDBMake, "finish", rb_cdbmake_finish, 0);
}



