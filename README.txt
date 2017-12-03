Fermenter Controller Version 1
==============================

Usage: Start the daemon in rc.local. Standard output/error can be re-routed to a log file, e.g.:
	/usr/local/sbin/fermenter >> /var/log/fermenter_log 2>&1 &

Examine the current programme for each fermenter by reading the lock files: 
	cat /var/tmp/fermenterN (where N is 0 or 1)
If the file is present, a programme is running for that fermenter. The single line in
the file consists of the start time (in Unix format) and the file name of the programme.
	
Control the fermenter using the FIFOs:
	/var/tmp/fermenter.out and
	/var/tmp/fermenter.in
This can be achieved by running 'cat /var/tmp/fermenter.out' in one terminal and
'echo -n "command" >> /var/tmp/fermenter.out' in another.
	
Send the followiung commands to fermenter.in, output (if any) will be written to fermenter.out:
	v: Daemon responds with version information.
	q: Quits the daemon.
	pNfilename: Start programme from file 'filename' on fermenter N. Use the full path.
	sN: Stop programme on fermenter N.
	iN: Give information about programme on fermenter N.

Log files are written containing desired/actual temperature and time in 
	/var/www/html/fermenter/fermenterN_MMM.csv (where N is the fermenter number and MMM is the run number)
	The run numbers are 000 for the current run, 001 for the previous run, etc.
	
A web interface is available at http://my_host/fermenter
