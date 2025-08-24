shell-facts: main.c
	gcc -lsqlite3 main.c cjson/cJSON.c cjson/cJSON_Utils.c -o shell_facts

run:
	@@./shell_facts
