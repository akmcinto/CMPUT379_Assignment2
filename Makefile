procnanny: procnanny.c
	gcc -Wall -DMEMWATCH -DMW_STDIO procnanny.c memwatch.c -o procnanny

.PHONY: clean
clean:
	-rm -f procnanny *.o core
