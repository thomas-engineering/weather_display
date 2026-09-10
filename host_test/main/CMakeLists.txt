idf_component_register(
    SRCS "test_main.c" "test_light_policy.c" "test_storage_record.c" "fake_storage_backend.c"
    INCLUDE_DIRS "."
    REQUIRES unity app_logic
)
