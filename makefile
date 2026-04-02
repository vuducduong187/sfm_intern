all:
	gcc -o sfmd sfmd.c -static
	gcc -o sfmbkd sfmbkd.c -static
	gcc -o Pm_1 pm_1.c -static
	gcc -o Pm_2 pm_2.c -static
clean:
	rm -f sfmd sfmbkd Pm_1 Pm_2 sfm_cur_stt.conf /tmp/FS.sfmd
