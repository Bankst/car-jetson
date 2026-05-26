// Shim: openssl/engine.h removed in OpenSSL 3.x. aasdk calls ENGINE_cleanup() once.
#define ENGINE_cleanup() ((void)0)
