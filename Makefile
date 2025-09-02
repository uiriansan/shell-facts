shell-facts: src/main.c
	gcc -O3 src/main.c include/vendor/cjson/cJSON.c include/vendor/cjson/cJSON_Utils.c -I./include/ `pkg-config --cflags --libs chafa` -lsqlite3 -lm -o shell-facts

run:
	@@./shell-facts
