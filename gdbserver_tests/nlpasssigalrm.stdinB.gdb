# connect gdb to Valgrind gdbserver:
target remote | ./vgdb --wait=60 --vgdb-prefix=./vgdb-prefix-nlpasssigalrm
echo vgdb launched process attached\n
monitor v.set vgdb-error 999999
break passsigalrm.c:43
break passsigalrm.c:44
#
#
# ensure SIGALRM can be passed directly to the process, without
# going through gdb:
handle SIGALRM stop print pass
#
continue
#
# Here, gdb should have been informed of the 1st SIGALRM
# Tell the 2nd can be given directly
handle SIGALRM nostop noprint pass
continue
# Here, we expect to stop on the breakme
p breakme
continue
p breakme
continue
quit
