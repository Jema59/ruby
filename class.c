/************************************************

  class.c -

  $Author$
  $Date$
  created at: Tue Aug 10 15:05:44 JST 1993

  Copyright (C) 1993-1998 Yukihiro Matsumoto

************************************************/

#include "ruby.h"
#include "node.h"
#include "st.h"
#include <ctype.h>

#ifdef USE_CWGUSI
#include <stdio.h>
#endif

extern st_table *rb_class_tbl;

VALUE
class_new(super)
    VALUE super;
{
    NEWOBJ(klass, struct RClass);
    OBJSETUP(klass, cClass, T_CLASS);

    klass->super = super;
    klass->iv_tbl = 0;
    klass->m_tbl = 0;		/* safe GC */
    klass->m_tbl = new_idhash();

    return (VALUE)klass;
}

VALUE
singleton_class_new(super)
    VALUE super;
{
    VALUE klass = class_new(super);

    FL_SET(klass, FL_SINGLETON);
    return klass;
}

static int
clone_method(mid, body, tbl)
    ID mid;
    NODE *body;
    st_table *tbl;
{
    st_insert(tbl, mid, NEW_METHOD(body->nd_body, body->nd_noex));
    return ST_CONTINUE;
}

VALUE
singleton_class_clone(klass)
    VALUE klass;
{
    if (!FL_TEST(klass, FL_SINGLETON))
	return klass;
    else {
	/* copy singleton(unnamed) class */
	NEWOBJ(clone, struct RClass);
	CLONESETUP(clone, klass);

	clone->super = RCLASS(klass)->super;
	clone->iv_tbl = 0;
	clone->m_tbl = 0;
	clone->m_tbl = new_idhash();
	st_foreach(RCLASS(klass)->m_tbl, clone_method, clone->m_tbl);
	FL_SET(clone, FL_SINGLETON);
	return (VALUE)clone;
    }
}

void
singleton_class_attached(klass, obj)
    VALUE klass, obj;
{
    if (FL_TEST(klass, FL_SINGLETON))
	rb_iv_set(klass, "__attached__", obj);
}

VALUE
rb_define_class_id(id, super)
    ID id;
    VALUE super;
{
    VALUE klass;

    if (!super) super = cObject;
    klass = class_new(super);
    rb_name_class(klass, id);
    /* make metaclass */
    RBASIC(klass)->klass = singleton_class_new(RBASIC(super)->klass);
    singleton_class_attached(RBASIC(klass)->klass, klass);
    rb_funcall(super, rb_intern("inherited"), 1, klass);

    return klass;
}

VALUE
rb_define_class(name, super)
    char *name;
    VALUE super;
{
    VALUE klass;
    ID id;

    id = rb_intern(name);
    klass = rb_define_class_id(id, super);
    st_add_direct(rb_class_tbl, id, klass);

    return klass;
}

VALUE
rb_define_class_under(outer, name, super)
    VALUE outer;
    char *name;
    VALUE super;
{
    VALUE klass;
    ID id;

    id = rb_intern(name);
    klass = rb_define_class_id(id, super);
    rb_const_set(outer, id, klass);
    rb_set_class_path(klass, outer, name);

    return klass;
}

VALUE
module_new()
{
    NEWOBJ(mdl, struct RClass);
    OBJSETUP(mdl, cModule, T_MODULE);

    mdl->super = 0;
    mdl->iv_tbl = 0;
    mdl->m_tbl = 0;
    mdl->m_tbl = new_idhash();

    return (VALUE)mdl;
}

VALUE
rb_define_module_id(id)
    ID id;
{
    extern st_table *rb_class_tbl;
    VALUE mdl = module_new();

    rb_name_class(mdl, id);

    return mdl;
}

VALUE
rb_define_module(name)
    char *name;
{
    VALUE module;
    ID id;

    id = rb_intern(name);
    module = rb_define_module_id(id);
    st_add_direct(rb_class_tbl, id, module);

    return module;
}

VALUE
rb_define_module_under(outer, name)
    VALUE outer;
    char *name;
{
    VALUE module;
    ID id;

    id = rb_intern(name);
    module = rb_define_module_id(id);
    rb_const_set(outer, id, module);
    rb_set_class_path(module, outer, name);

    return module;
}

static VALUE
include_class_new(module, super)
    VALUE module, super;
{
    NEWOBJ(klass, struct RClass);
    OBJSETUP(klass, cClass, T_ICLASS);

    if (!RCLASS(module)->iv_tbl) {
	RCLASS(module)->iv_tbl = st_init_numtable();
    }
    klass->iv_tbl = RCLASS(module)->iv_tbl;
    klass->m_tbl = RCLASS(module)->m_tbl;
    klass->super = super;
    if (TYPE(module) == T_ICLASS) {
	RBASIC(klass)->klass = RBASIC(module)->klass;
    }
    else {
	RBASIC(klass)->klass = module;
    }

    return (VALUE)klass;
}

void
rb_include_module(klass, module)
    VALUE klass, module;
{
    VALUE p;

    if (NIL_P(module)) return;
    if (klass == module) return;

    switch (TYPE(module)) {
      case T_MODULE:
      case T_CLASS:
      case T_ICLASS:
	break;
      default:
	Check_Type(module, T_MODULE);
    }

    while (module) {
	/* ignore if the module included already in superclasses */
	for (p = RCLASS(klass)->super; p; p = RCLASS(p)->super) {
	    if (BUILTIN_TYPE(p) == T_ICLASS &&
		RCLASS(p)->m_tbl == RCLASS(module)->m_tbl) {
		if (RCLASS(module)->super) {
		    rb_include_module(p, RCLASS(module)->super);
		}
		return;
	    }
	}
	RCLASS(klass)->super =
	    include_class_new(module, RCLASS(klass)->super);
	klass = RCLASS(klass)->super;
	module = RCLASS(module)->super;
    }
    rb_clear_cache();
}

VALUE
mod_included_modules(mod)
    VALUE mod;
{
    VALUE ary = ary_new();
    VALUE p;

    for (p = RCLASS(mod)->super; p; p = RCLASS(p)->super) {
	if (BUILTIN_TYPE(p) == T_ICLASS) {
	    ary_push(ary, RBASIC(p)->klass);
	}
    }
    return ary;
}

VALUE
mod_ancestors(mod)
    VALUE mod;
{
    VALUE ary = ary_new();
    VALUE p;

    for (p = mod; p; p = RCLASS(p)->super) {
	if (FL_TEST(p, FL_SINGLETON))
	    continue;
	if (BUILTIN_TYPE(p) == T_ICLASS) {
	    ary_push(ary, RBASIC(p)->klass);
	}
	else {
	    ary_push(ary, p);
	}
    }
    return ary;
}

static int
ins_methods_i(key, body, ary)
    ID key;
    NODE *body;
    VALUE ary;
{
    if ((body->nd_noex&(NOEX_PRIVATE|NOEX_PROTECTED)) == 0) {
	VALUE name = str_new2(rb_id2name(key));

	if (!ary_includes(ary, name)) {
	    if (!body->nd_body) {
		ary_push(ary, Qnil);
	    }
	    ary_push(ary, name);
	}
    }
    else if (body->nd_body && nd_type(body->nd_body) == NODE_ZSUPER) {
	ary_push(ary, Qnil);
	ary_push(ary, str_new2(rb_id2name(key)));
    }
    return ST_CONTINUE;
}

static int
ins_methods_prot_i(key, body, ary)
    ID key;
    NODE *body;
    VALUE ary;
{
    if (!body->nd_body) {
	ary_push(ary, Qnil);
	ary_push(ary, str_new2(rb_id2name(key)));
    }
    else if (body->nd_noex & NOEX_PROTECTED) {
	VALUE name = str_new2(rb_id2name(key));

	if (!ary_includes(ary, name)) {
	    ary_push(ary, name);
	}
    }
    else if (nd_type(body->nd_body) == NODE_ZSUPER) {
	ary_push(ary, Qnil);
	ary_push(ary, str_new2(rb_id2name(key)));
    }
    return ST_CONTINUE;
}

static int
ins_methods_priv_i(key, body, ary)
    ID key;
    NODE *body;
    VALUE ary;
{
    if (!body->nd_body) {
	ary_push(ary, Qnil);
	ary_push(ary, str_new2(rb_id2name(key)));
    }
    else if (body->nd_noex & NOEX_PRIVATE) {
	VALUE name = str_new2(rb_id2name(key));

	if (!ary_includes(ary, name)) {
	    ary_push(ary, name);
	}
    }
    else if (nd_type(body->nd_body) == NODE_ZSUPER) {
	ary_push(ary, Qnil);
	ary_push(ary, str_new2(rb_id2name(key)));
    }
    return ST_CONTINUE;
}

static VALUE
method_list(mod, option, func)
    VALUE mod;
    int option;
    int (*func)();
{
    VALUE ary;
    VALUE klass;
    VALUE *p, *q, *pend;

    ary = ary_new();
    for (klass = mod; klass; klass = RCLASS(klass)->super) {
	st_foreach(RCLASS(klass)->m_tbl, func, ary);
	if (!option) break;
    }
    p = q = RARRAY(ary)->ptr; pend = p + RARRAY(ary)->len;
    while (p < pend) {
	if (*p == Qnil) {
	    p+=2;
	    continue;
	}
	*q++ = *p++;
    }
    RARRAY(ary)->len = q - RARRAY(ary)->ptr;
    return ary;
}

VALUE
class_instance_methods(argc, argv, mod)
    int argc;
    VALUE *argv;
    VALUE mod;
{
    VALUE option;

    rb_scan_args(argc, argv, "01", &option);
    return method_list(mod, RTEST(option), ins_methods_i);
}

VALUE
class_protected_instance_methods(argc, argv, mod)
    int argc;
    VALUE *argv;
    VALUE mod;
{
    VALUE option;

    rb_scan_args(argc, argv, "01", &option);
    return method_list(mod, RTEST(option), ins_methods_prot_i);
}

VALUE
class_private_instance_methods(argc, argv, mod)
    int argc;
    VALUE *argv;
    VALUE mod;
{
    VALUE option;

    rb_scan_args(argc, argv, "01", &option);
    return method_list(mod, RTEST(option), ins_methods_priv_i);
}

VALUE
obj_singleton_methods(obj)
    VALUE obj;
{
    VALUE ary;
    VALUE klass;
    VALUE *p, *q, *pend;

    ary = ary_new();
    klass = CLASS_OF(obj);
    while (klass && FL_TEST(klass, FL_SINGLETON)) {
	st_foreach(RCLASS(klass)->m_tbl, ins_methods_i, ary);
	klass = RCLASS(klass)->super;
    }
    p = q = RARRAY(ary)->ptr; pend = p + RARRAY(ary)->len;
    while (p < pend) {
	if (*p == Qnil) {
	    p+=2;
	    continue;
	}
	*q++ = *p++;
    }
    RARRAY(ary)->len = q - RARRAY(ary)->ptr;

    return ary;
}

void
rb_define_method_id(klass, name, func, argc)
    VALUE klass;
    ID name;
    VALUE (*func)();
    int argc;
{
    rb_add_method(klass, name, NEW_CFUNC(func,argc), NOEX_PUBLIC|NOEX_CFUNC);
}

void
rb_define_method(klass, name, func, argc)
    VALUE klass;
    char *name;
    VALUE (*func)();
    int argc;
{
    ID id = rb_intern(name);

    rb_add_method(klass, id, NEW_CFUNC(func, argc), 
		  ((name[0] == 'i' && id == rb_intern("initialize"))?
		   NOEX_PRIVATE:NOEX_PUBLIC)|NOEX_CFUNC);
}

void
rb_define_protected_method(klass, name, func, argc)
    VALUE klass;
    char *name;
    VALUE (*func)();
    int argc;
{
    rb_add_method(klass, rb_intern(name), NEW_CFUNC(func, argc),
		  NOEX_PROTECTED|NOEX_CFUNC);
}

void
rb_define_private_method(klass, name, func, argc)
    VALUE klass;
    char *name;
    VALUE (*func)();
    int argc;
{
    rb_add_method(klass, rb_intern(name), NEW_CFUNC(func, argc),
		  NOEX_PRIVATE|NOEX_CFUNC);
}

void
rb_undef_method(klass, name)
    VALUE klass;
    char *name;
{
    rb_add_method(klass, rb_intern(name), 0, NOEX_UNDEF);
}

VALUE
rb_singleton_class(obj)
    VALUE obj;
{
    if (rb_special_const_p(obj)) {
	TypeError("cannot define singleton");
    }
    if (FL_TEST(RBASIC(obj)->klass, FL_SINGLETON)) {
	return RBASIC(obj)->klass;
    }
    RBASIC(obj)->klass = singleton_class_new(RBASIC(obj)->klass);
    singleton_class_attached(RBASIC(obj)->klass, obj);
    return RBASIC(obj)->klass;
}

void
rb_define_singleton_method(obj, name, func, argc)
    VALUE obj;
    char *name;
    VALUE (*func)();
    int argc;
{
    rb_define_method(rb_singleton_class(obj), name, func, argc);
}

void
rb_define_module_function(module, name, func, argc)
    VALUE module;
    char *name;
    VALUE (*func)();
    int argc;
{
    rb_define_private_method(module, name, func, argc);
    rb_define_singleton_method(module, name, func, argc);
}

void
rb_define_global_function(name, func, argc)
    char *name;
    VALUE (*func)();
    int argc;
{
    rb_define_private_method(mKernel, name, func, argc);
}

void
rb_define_alias(klass, name1, name2)
    VALUE klass;
    char *name1, *name2;
{
    rb_alias(klass, rb_intern(name1), rb_intern(name2));
}

void
rb_define_attr(klass, name, read, write)
    VALUE klass;
    char *name;
    int read, write;
{
    rb_attr(klass, rb_intern(name), read, write, FALSE);
}

#ifdef HAVE_STDARG_PROTOTYPES
#include <stdarg.h>
#define va_init_list(a,b) va_start(a,b)
#else
#include <varargs.h>
#define va_init_list(a,b) va_start(a)
#endif

int
#ifdef HAVE_STDARG_PROTOTYPES
rb_scan_args(int argc, VALUE *argv, char *fmt, ...)
#else
rb_scan_args(argc, argv, fmt, va_alist)
    int argc;
    VALUE *argv;
    char *fmt;
    va_dcl
#endif
{
    int n, i;
    char *p = fmt;
    VALUE *var;
    va_list vargs;

    va_init_list(vargs, fmt);

    if (*p == '*') {
	var = va_arg(vargs, VALUE*);
	*var = ary_new4(argc, argv);
	return argc;
    }

    if (ISDIGIT(*p)) {
	n = *p - '0';
	if (n > argc)
	    ArgError("Wrong # of arguments (%d for %d)", argc, n);
	for (i=0; i<n; i++) {
	    var = va_arg(vargs, VALUE*);
	    *var = argv[i];
	}
	p++;
    }
    else {
	goto error;
    }

    if (ISDIGIT(*p)) {
	n = i + *p - '0';
	for (; i<n; i++) {
	    var = va_arg(vargs, VALUE*);
	    if (argc > i) {
		*var = argv[i];
	    }
	    else {
		*var = Qnil;
	    }
	}
	p++;
    }

    if(*p == '*') {
	var = va_arg(vargs, VALUE*);
	if (argc > i) {
	    *var = ary_new4(argc-i, argv+i);
	}
	else {
	    *var = ary_new();
	}
    }
    else if (*p == '\0') {
	if (argc > i) {
	    ArgError("Wrong # of arguments(%d for %d)", argc, i);
	}
    }
    else {
	goto error;
    }

    va_end(vargs);
    return argc;

  error:
    Fatal("bad scan arg format: %s", fmt);
    return 0;
}
