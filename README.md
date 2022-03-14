# netmenu
a netctl controller / deamon 

### 1. Use systemctl to start netctld
The commond is ```sudo systemctl start netmenu```, considering the old installation you may need stop it at first(using ```sudo systemctl stop netmenu```).
If you want it runs when you computer starts up, run ```sudo systemctl enable netmenu```.

### 2. Run netmenu
You can run dmenu first or run netmenu directly, then dmenu bar will show your network configures under ```/etc/netctl``` directory. 
Use left/right arrow choose a network, press enter to connect.

### 3. Add new network configure
Choose ```+ new interface```, netmenu will open a terminal to start wifi-menu (need install), the default terminal is urxvt. To use you own terminal, simply put a
symlink into some directory in your path by running ```ln -s <source> <target>```
