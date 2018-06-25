zephyr_library()

zephyr_library_sources(
  vector_table.S
  reset.S
  prep_c.c
  fault.c
  scb.c
  exc_manage.c
  irq_init.c
  thread_abort.c
  )
