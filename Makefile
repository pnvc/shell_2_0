OBJECTS:=process_control.o Shell_2_0.o help_functions.o history_control.o input_formatting.o mk_full_hist_file_path.o
COMPILER=gcc
COMPILER_ARGS:=-Wall -g -c

Shell_2_0 : $(OBJECTS)
	$(COMPILER) $(OBJECTS) -o $@
	
process_control.o:
	$(COMPILER) $(COMPILER_ARGS) modules/process_control.c

Shell_2_0.o:
	$(COMPILER) $(COMPILER_ARGS) Shell_2_0.c

help_functions.o:
	$(COMPILER) $(COMPILER_ARGS) modules/help_functions.c

history_control.o:
	$(COMPILER) $(COMPILER_ARGS) modules/history_control.c
	
input_formatting.o:
	$(COMPILER) $(COMPILER_ARGS) modules/input_formatting.c

mk_full_hist_file_path.o:
	$(COMPILER) $(COMPILER_ARGS) modules/mk_full_hist_file_path.c

clean:
	rm $(OBJECTS)
