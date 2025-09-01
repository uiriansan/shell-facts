shell-facts: main.c
	gcc main.c cjson/cJSON.c cjson/cJSON_Utils.c `pkg-config --cflags --libs chafa` -lsqlite3 -lm -o shell-facts

run:
	@@./shell-facts
