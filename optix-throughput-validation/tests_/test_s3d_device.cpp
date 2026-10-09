/*
 * test_s3d_device.cpp — Ported from cus3d test_s3d_device.c for ox_s3d
 */
#include "s3d.h"
#include "test_s3d_utils.h"

static void
log_stream(const char* msg, void* ctx)
{
  ASSERT(msg);
  (void)msg; (void)ctx;
  printf("%s\n", msg);
}

int
main(int argc, char** argv)
{
  struct logger logger;
  struct mem_allocator allocator;
  struct s3d_device* dev;
  (void)argc; (void)argv;

  CHK(s3d_device_create(NULL, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(s3d_device_create(NULL, NULL, 0, &dev) == RES_OK);

  CHK(s3d_device_ref_get(NULL) == RES_BAD_ARG);
  CHK(s3d_device_ref_get(dev) == RES_OK);
  CHK(s3d_device_ref_put(NULL) == RES_BAD_ARG);
  CHK(s3d_device_ref_put(dev) == RES_OK);
  CHK(s3d_device_ref_put(dev) == RES_OK);

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(MEM_ALLOCATED_SIZE(&allocator) == 0);
  CHK(s3d_device_create(NULL, &allocator, 0, NULL) == RES_BAD_ARG);
  CHK(s3d_device_create(NULL, &allocator, 0, &dev) == RES_OK);
  CHK(s3d_device_ref_put(dev) == RES_OK);
  CHK(MEM_ALLOCATED_SIZE(&allocator) == 0);

  CHK(logger_init(&allocator, &logger) == RES_OK);
  logger_set_stream(&logger, LOG_OUTPUT, log_stream, NULL);
  logger_set_stream(&logger, LOG_ERROR, log_stream, NULL);
  logger_set_stream(&logger, LOG_WARNING, log_stream, NULL);

  CHK(s3d_device_create(&logger, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(s3d_device_create(&logger, NULL, 0, &dev) == RES_OK);
  CHK(s3d_device_ref_put(dev) == RES_OK);

  CHK(s3d_device_create(&logger, &allocator, 1, NULL) == RES_BAD_ARG);
  CHK(s3d_device_create(&logger, &allocator, 1, &dev) == RES_OK);
  CHK(s3d_device_ref_put(dev) == RES_OK);

  logger_release(&logger);
  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  printf("test_s3d_device: PASSED\n");
  return 0;
}
