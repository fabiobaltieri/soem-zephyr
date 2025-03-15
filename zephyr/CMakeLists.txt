if(CONFIG_SOEM)

set(soem_dir ${ZEPHYR_SOEM_MODULE_DIR})

zephyr_library_named(soem)

zephyr_include_directories(
	${soem_dir}/soem
	${soem_dir}/osal
	${soem_dir}/osal/zephyr
	${soem_dir}/oshw/zephyr
)

zephyr_library_sources(
	${soem_dir}/soem/ethercatbase.c
	${soem_dir}/soem/ethercatcoe.c
	${soem_dir}/soem/ethercatconfig.c
	${soem_dir}/soem/ethercatdc.c
	${soem_dir}/soem/ethercateoe.c
	${soem_dir}/soem/ethercatfoe.c
	${soem_dir}/soem/ethercatmain.c
	${soem_dir}/soem/ethercatprint.c
	${soem_dir}/soem/ethercatsoe.c
	${soem_dir}/osal/zephyr/osal.c
	${soem_dir}/oshw/zephyr/nicdrv.c
	${soem_dir}/oshw/zephyr/oshw.c
)

endif()
