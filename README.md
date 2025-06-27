I needed a way to transfer files from my laptop to a Mac 6100 running MacOS 7.6.1. and OSX doesn't ship with an FTP server. I could have screwed around downloading an opensource ftp server but I'd rather code my own then reading someone else's doc explaining how to set it up and disable all the security features. Old Mac's don't support encrypted transfers so only use this if you need to work with ancient FTP clients! 

Download (-d) and upload (-u) directories are configurable when launching the server. -D will launch the server in daemon mode with logs generated in /tmp. In non-daemon mode, logging prints to standard out. 

This is a multithreaded service supporting up to 512 concurrent connections.

(c)June 2025 - mikewolak@gmail.com
