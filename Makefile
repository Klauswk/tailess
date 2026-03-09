tailess: tailess.c hotui.h
	cc -ggdb -Wall -Wextra tailess.c -o tailess 

.PHONY: install
install: tailess
	mkdir -p ~/opt/ && cp ./tailess ~/opt/
