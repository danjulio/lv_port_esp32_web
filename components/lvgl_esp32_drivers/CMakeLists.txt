file(GLOB SOURCES *.c)

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS .
                       EMBED_FILES index.html favicon.ico)
                       REQUIRES lvgl)
