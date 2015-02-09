/*
  +----------------------------------------------------------------------+
  | Yet Another Framework                                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Xinchen Hui  <laruence@php.net>                              |
  +----------------------------------------------------------------------+
*/

/* $Id: http.c 329197 2013-01-18 05:55:37Z laruence $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "main/SAPI.h"
#include "ext/standard/url.h" /* for php_url */

#include "php_yaf.h"
#include "yaf_namespace.h"
#include "yaf_request.h"
#include "yaf_exception.h"

#include "requests/yaf_request_http.h"

static zend_class_entry * yaf_request_http_ce;

/** {{{ yaf_request_t * yaf_request_http_instance(yaf_request_t *this_ptr, char *request_uri, char *base_uri)
*/
yaf_request_t * yaf_request_http_instance(yaf_request_t *this_ptr, char *request_uri, char *base_uri) {
	yaf_request_t *instance;
	zval method, params, *settled_uri = NULL, rsettled_uri;

	instance = this_ptr;
	if (ZVAL_IS_NULL(this_ptr)) {
		object_init_ex(instance, yaf_request_http_ce);
	}

	if (SG(request_info).request_method) {
		ZVAL_STRING(&method, (char *)SG(request_info).request_method);
	} else if (strncasecmp(sapi_module.name, "cli", 3)) {
		ZVAL_STRING(&method, "Unknow");
	} else {
		ZVAL_STRING(&method, "Cli");
	}

	zend_update_property(yaf_request_http_ce, instance, ZEND_STRL(YAF_REQUEST_PROPERTY_NAME_METHOD), &method);
	zval_ptr_dtor(&method);

	if (request_uri) {
		ZVAL_STRING(&rsettled_uri, request_uri);
		settled_uri = &rsettled_uri;
	} else {
		zval *uri, *rewrited;
		zend_string *name;
		do {
#ifdef PHP_WIN32
			/* check this first so IIS will catch */
			name = zend_string_init("HTTP_X_REWRITE_URL", sizeof("HTTP_X_REWRITE_URL") - 1, 0);
			uri = yaf_request_query(YAF_GLOBAL_VARS_SERVER, name);
			zend_string_release(name);
			if (uri && Z_TYPE_P(uri) != IS_NULL) {
				settled_uri = uri;
				zval_ptr_dtor(uri);
				break;
			}

			/* IIS7 with URL Rewrite: make sure we get the unencoded url (double slash problem) */
			name = zend_string_init("IIS_WasUrlRewritten", sizeof("IIS_WasUrlRewritten") - 1, 0);
			rewrited = yaf_request_query(YAF_GLOBAL_VARS_SERVER, name);
			zend_string_release(name);
			if (rewrited) {
				zval *unencode;
				name = zend_string_init("UNENCODED_URL", sizeof("UNENCODED_URL") - 1, 0);
				uri = yaf_request_query(YAF_GLOBAL_VARS_SERVER, name);
				zend_string_release(name);
				if (unencode && zval_get_long(rewrited) 
						&& Z_TYPE_P(unencode) == IS_STRING
						&& Z_STRLEN_P(unencode) > 0) {
					settled_uri = uri;
				}
				zval_ptr_dtor(uri);
				break;
			}
#endif
			name = zend_string_init("PATH_INFO", sizeof("PATH_INFO") - 1, 0);
			uri = yaf_request_query(YAF_GLOBAL_VARS_SERVER, name);
			zend_string_release(name);
			if (uri && Z_TYPE_P(uri) != IS_NULL) {
				settled_uri = uri;
				zval_ptr_dtor(uri);
				break;
			}

			name = zend_string_init("REQUEST_URI", sizeof("REQUEST_URI") - 1, 0);
			uri = yaf_request_query(YAF_GLOBAL_VARS_SERVER, name);
			zend_string_release(name);
			if (uri && Z_TYPE_P(uri) != IS_NULL) {
				/* Http proxy reqs setup request uri with scheme and host [and port] + the url path, only use url path */
				if (strstr(Z_STRVAL_P(uri), "http") == Z_STRVAL_P(uri)) {
					php_url *url_info = php_url_parse(Z_STRVAL_P(uri));
					zval_ptr_dtor(uri);

					if (url_info && url_info->path) {
						ZVAL_STRING(&rsettled_uri, url_info->path);
						settled_uri = &rsettled_uri;
					}

					php_url_free(url_info);
				} else {
					char *pos  = NULL;
					if ((pos = strstr(Z_STRVAL_P(uri), "?"))) {
						ZVAL_STRINGL(&rsettled_uri, Z_STRVAL_P(uri), pos - Z_STRVAL_P(uri));
						settled_uri = &rsettled_uri;
						zval_ptr_dtor(uri);
					} else {
						settled_uri = uri;
					}
				}
				zval_ptr_dtor(uri);
				break;
			}

			name = zend_string_init("ORIG_PATH_INFO", sizeof("ORIG_PATH_INFO") - 1, 0);
			uri = yaf_request_query(YAF_GLOBAL_VARS_SERVER, name);
			zend_string_release(name);
			if (uri && Z_TYPE_P(uri) != IS_NULL) {
				settled_uri = uri;
				zval_ptr_dtor(uri);
				break;
			}

		} while (0);
	}

	if (settled_uri && Z_TYPE_P(settled_uri) == IS_STRING) {
		char *p = Z_STRVAL_P(settled_uri);

		while (*p == '/' && *(p + 1) == '/') {
			p++;
		}

		if (p != Z_STRVAL_P(settled_uri)) {
			zend_string *garbage = Z_STR_P(settled_uri);
			ZVAL_STRING(settled_uri, p);
			zend_string_release(garbage);
		}

		zend_update_property(yaf_request_http_ce, instance, ZEND_STRL(YAF_REQUEST_PROPERTY_NAME_URI), settled_uri);
		yaf_request_set_base_uri(instance, base_uri, Z_STRVAL_P(settled_uri));
		zval_ptr_dtor(settled_uri);
	}

	array_init(&params);
	zend_update_property(yaf_request_http_ce, instance, ZEND_STRL(YAF_REQUEST_PROPERTY_NAME_PARAMS), &params);
	zval_ptr_dtor(&params);

	return instance;
}
/* }}} */

/** {{{ proto public Yaf_Request_Http::getQuery(mixed $name, mixed $default = NULL)
*/
YAF_REQUEST_METHOD(yaf_request_http, Query, 	YAF_GLOBAL_VARS_GET);
/* }}} */

/** {{{ proto public Yaf_Request_Http::getPost(mixed $name, mixed $default = NULL)
*/
YAF_REQUEST_METHOD(yaf_request_http, Post,  	YAF_GLOBAL_VARS_POST);
/* }}} */

/** {{{ proto public Yaf_Request_Http::getRequet(mixed $name, mixed $default = NULL)
*/
YAF_REQUEST_METHOD(yaf_request_http, Request, YAF_GLOBAL_VARS_REQUEST);
/* }}} */

/** {{{ proto public Yaf_Request_Http::getFiles(mixed $name, mixed $default = NULL)
*/
YAF_REQUEST_METHOD(yaf_request_http, Files, 	YAF_GLOBAL_VARS_FILES);
/* }}} */

/** {{{ proto public Yaf_Request_Http::getCookie(mixed $name, mixed $default = NULL)
*/
YAF_REQUEST_METHOD(yaf_request_http, Cookie, 	YAF_GLOBAL_VARS_COOKIE);
/* }}} */

/** {{{ proto public Yaf_Request_Http::isXmlHttpRequest()
*/
PHP_METHOD(yaf_request_http, isXmlHttpRequest) {
	zend_string *name;
	zval * header;
	name = zend_string_init("HTTP_X_REQUESTED_WITH", sizeof("HTTP_X_REQUESTED_WITH") - 1, 0);
	header = yaf_request_query(YAF_GLOBAL_VARS_SERVER, name);
	zend_string_release(name);
	if (header && Z_TYPE_P(header) == IS_STRING
			&& strncasecmp("XMLHttpRequest", Z_STRVAL_P(header), Z_STRLEN_P(header)) == 0) {
		zval_ptr_dtor(header);
		RETURN_TRUE;
	}
	RETURN_FALSE;
}
/* }}} */

/** {{{ proto public Yaf_Request_Http::get(mixed $name, mixed $default)
 * params -> post -> get -> cookie -> server
 */
PHP_METHOD(yaf_request_http, get) {
	zend_string	*name 	= NULL;
	zval 	*def 	= NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "S|z", &name, &def) == FAILURE) {
		WRONG_PARAM_COUNT;
	} else {
		zval *value = yaf_request_get_param(getThis(), name);
		if (value) {
			RETURN_ZVAL(value, 1, 0);
		} else {
			zval *params	= NULL;
			zval *pzval	= NULL;

			YAF_GLOBAL_VARS_TYPE methods[4] = {
				YAF_GLOBAL_VARS_POST,
				YAF_GLOBAL_VARS_GET,
				YAF_GLOBAL_VARS_COOKIE,
				YAF_GLOBAL_VARS_SERVER
			};

			{
				int i = 0;
				for (;i<4; i++) {
					params = &PG(http_globals)[methods[i]];
					if (params && Z_TYPE_P(params) == IS_ARRAY) {
						if ((pzval = zend_hash_find(Z_ARRVAL_P(params), name)) != NULL ){
							RETURN_ZVAL(pzval, 1, 0);
						}
					}
				}

			}
			if (def) {
				RETURN_ZVAL(def, 1, 0);
			}
		}
	}
	RETURN_NULL();
}
/* }}} */

/** {{{ proto public Yaf_Request_Http::__construct(string $request_uri, string $base_uri)
*/
PHP_METHOD(yaf_request_http, __construct) {
	char *request_uri = NULL;
	char *base_uri = NULL;
	size_t rlen = 0;
	size_t blen = 0;
	yaf_request_t rself, *self = getThis();

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "|ss", &request_uri, &rlen, &base_uri, &blen) == FAILURE) {
		YAF_UNINITIALIZED_OBJECT(getThis());
		return;
	}

	if (!self) {
		ZVAL_NULL(&rself);
		self = &rself;
	}
	(void)yaf_request_http_instance(self, request_uri, base_uri);
}
/* }}} */

/** {{{ proto private Yaf_Request_Http::__clone
 */
PHP_METHOD(yaf_request_http, __clone) {
}
/* }}} */

/** {{{ yaf_request_http_methods
 */
zend_function_entry yaf_request_http_methods[] = {
	PHP_ME(yaf_request_http, getQuery, 		NULL, ZEND_ACC_PUBLIC)
	PHP_ME(yaf_request_http, getRequest, 		NULL, ZEND_ACC_PUBLIC)
	PHP_ME(yaf_request_http, getPost, 		NULL, ZEND_ACC_PUBLIC)
	PHP_ME(yaf_request_http, getCookie,		NULL, ZEND_ACC_PUBLIC)
	PHP_ME(yaf_request_http, getFiles,		NULL, ZEND_ACC_PUBLIC)
	PHP_ME(yaf_request_http, get,			NULL, ZEND_ACC_PUBLIC)
	PHP_ME(yaf_request_http, isXmlHttpRequest, 	NULL, ZEND_ACC_PUBLIC)
	PHP_ME(yaf_request_http, __construct,		NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	PHP_ME(yaf_request_http, __clone,		NULL, ZEND_ACC_PRIVATE | ZEND_ACC_CLONE)
	{NULL, NULL, NULL}
};
/* }}} */

/** {{{ YAF_STARTUP_FUNCTION
 */
YAF_STARTUP_FUNCTION(request_http){
	zend_class_entry ce;
	YAF_INIT_CLASS_ENTRY(ce, "Yaf_Request_Http", "Yaf\\Request\\Http", yaf_request_http_methods);
	yaf_request_http_ce = zend_register_internal_class_ex(&ce, yaf_request_ce);

	zend_declare_class_constant_string(yaf_request_ce, ZEND_STRL("SCHEME_HTTP"), "http");
	zend_declare_class_constant_string(yaf_request_ce, ZEND_STRL("SCHEME_HTTPS"), "https");

	return SUCCESS;
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
