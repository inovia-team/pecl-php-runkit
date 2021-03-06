/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2006 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.0 of the PHP license,       |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_0.txt.                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Sara Golemon <pollita@php.net>                               |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#include "php_runkit.h"

#ifdef PHP_RUNKIT_MANIPULATION
/* {{{ php_runkit_remove_inherited_methods */
static int php_runkit_remove_inherited_methods(zend_function *fe, zend_class_entry *ce TSRMLS_DC)
{
	char *function_name = (char*)fe->common.function_name;
	int function_name_len = strlen(function_name);
	zend_class_entry *ancestor_class = php_runkit_locate_scope(ce, fe, function_name, function_name_len);

	if (ancestor_class == ce) {
		return ZEND_HASH_APPLY_KEEP;
	}

	zend_hash_apply_with_arguments(EG(class_table) ZEND_HASH_APPLY_ARGS_TSRMLS_CC, (apply_func_args_t)php_runkit_clean_children_methods, 4, ancestor_class, ce, function_name, function_name_len);

	/* Since we're about to remove this scope shift is *almost*
	 * entirely thrown away, except that reflecion_remove() uses
	 * it for declaring the scope of the zombie function
	 */
	fe->common.scope = ce;
	php_runkit_function_reflection_remove(fe TSRMLS_CC);
	php_runkit_del_magic_method(ce, fe);

	return ZEND_HASH_APPLY_REMOVE;
}
/* }}} */

/* {{{ proto bool runkit_class_emancipate(string classname)
	Convert an inherited class to a base class, removes any method whose scope is ancestral */
PHP_FUNCTION(runkit_class_emancipate)
{
	zend_class_entry *ce;
	char *classname;
	int classname_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s/", &classname, &classname_len) == FAILURE) {
		RETURN_FALSE;
	}

	if (php_runkit_fetch_class(classname, classname_len, &ce TSRMLS_CC) == FAILURE) {
		RETURN_FALSE;
	}

	if (!ce->parent) {
		php_error_docref(NULL TSRMLS_CC, E_NOTICE, "Class %s has no parent to emancipate from", classname);
		RETURN_TRUE;
	}

	zend_hash_apply_with_argument(&ce->function_table, (apply_func_arg_t)php_runkit_remove_inherited_methods, ce TSRMLS_CC);
	ce->parent = NULL;

	RETURN_TRUE;
}
/* }}} */

/* {{{ php_runkit_inherit_methods
	Inherit methods from a new ancestor */
static int php_runkit_inherit_methods(zend_function *fe, zend_class_entry *ce TSRMLS_DC)
{
	int function_name_len = strlen(fe->common.function_name);
	char *function_name = estrndup(fe->common.function_name, function_name_len);
	zend_class_entry *ancestor_class;
	zend_function fecopy;

	php_strtolower(function_name, function_name_len);
	ancestor_class = php_runkit_locate_scope(ce, fe, function_name, function_name_len);

	if (zend_hash_exists(&ce->function_table, function_name, function_name_len + 1)) {
		efree(function_name);
		return ZEND_HASH_APPLY_KEEP;
	}

	zend_hash_apply_with_arguments(EG(class_table) ZEND_HASH_APPLY_ARGS_TSRMLS_CC, (apply_func_args_t)php_runkit_update_children_methods, 5, ancestor_class, ce, fe, function_name, function_name_len);

	fecopy = *fe;
	fe = &fecopy;
	php_runkit_function_copy_ctor(fe, NULL);

	if (zend_hash_add(&ce->function_table, function_name, function_name_len + 1, fe, sizeof(zend_function), (void**)&fe) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error inheriting parent method: %s()", fe->common.function_name);
		efree(function_name);
		destroy_op_array((zend_op_array*)fe TSRMLS_CC);
		return ZEND_HASH_APPLY_KEEP;
	}
	efree(function_name);

	fe->common.prototype = fe;
	php_runkit_add_magic_method(ce, fe->common.function_name, fe);

	return ZEND_HASH_APPLY_KEEP;
}
/* }}} */

/* {{{ proto bool runkit_class_adopt(string classname, string parentname)
	Convert a base class to an inherited class, add ancestral methods when appropriate */
PHP_FUNCTION(runkit_class_adopt)
{
	zend_class_entry *ce, *parent;
	char *classname, *parentname;
	int classname_len, parentname_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s/s/", &classname, &classname_len, &parentname, &parentname_len) == FAILURE) {
		RETURN_FALSE;
	}

	if (php_runkit_fetch_class(classname, classname_len, &ce TSRMLS_CC) == FAILURE) {
		RETURN_FALSE;
	}

	if (ce->parent) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Class %s already has a parent", classname);
		RETURN_FALSE;
	}

	if (php_runkit_fetch_class(parentname, parentname_len, &parent TSRMLS_CC) == FAILURE) {
		RETURN_FALSE;
	}

	ce->parent = parent;
	zend_hash_apply_with_argument(&parent->function_table, (apply_func_arg_t)php_runkit_inherit_methods, ce TSRMLS_CC);

	/* TODO: Inherit properties */

	RETURN_TRUE;
}
/* }}} */

#endif /* PHP_RUNKIT_MANIPULATION */

#ifdef ZEND_ENGINE_2
/* {{{ proto int runkit_object_id(object instance)
Fetch the Object Handle ID from an instance */
PHP_FUNCTION(runkit_object_id)
{
	zval *obj;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "o", &obj) == FAILURE) {
		RETURN_NULL();
	}

	RETURN_LONG(Z_OBJ_HANDLE_P(obj));
}
/* }}} */
#endif /* ZEND_ENGINE_2 */
/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */

