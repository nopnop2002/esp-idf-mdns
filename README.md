# esp-idf-mdns
How to IP address resolution by mDNS.   

Since ESP-IDF Ver5, mDNS has been moved from built-in library to [this](https://components.espressif.com/components/espressif/mdns) IDF component registry.   
Accordingly, the mDNS official example has been removed from the ESP-IDF repository.   
The official repository for mDNS is [here](https://github.com/espressif/esp-protocols/tree/master/components/mdns).   


The official repository comes with example code, but it's a bit confusing.   
There are two methods for IP address resolution using mDNS.   
- IP address resolution by host name   
- IP address resolution by service name   

This is a quick example of each.   

# Software requiment
ESP-IDF V4.4/V5.x.   
ESP-IDF V5.0 is required when using ESP32-C2.   
ESP-IDF V5.1 is required when using ESP32-C6.   

# Hardware requirements
Requires two ESP32s.   


# IP address resolution by host name   
To find the IP address, you need to know the mDNS hostname.   
mDNS hostnames must be unique within the network.   
Write query-host1 to ESP32#1 and query-host2 to ESP32#2.   

### ESP32#1   
The mDNS name for this project is ```esp32-mdns1.local```.   
Then look for the the mDNS name of ```esp32-mdns2.local```.   
```
git clone https://github.com/nopnop2002/esp-idf-mdns
cd esp-idf-mdns/query-host1
idf.py menuconfig
idf.py flash
```



### ESP32#2   
The mDNS name for this project is ```esp32-mdns2.local```.   
Then look for the mDNS name of ```esp32-mdns1.local```.   
```
cd esp-idf-mdns/query-host2
idf.py menuconfig
idf.py flash
```

### Configuration
![config-top](https://user-images.githubusercontent.com/6020549/226929344-8410a99a-545d-4a88-8705-9842d3caf072.jpg)
![config-app-host](https://user-images.githubusercontent.com/6020549/226929353-f4d299a1-ca5c-4db8-aa4e-37ffb668bce5.jpg)

### Screen shot
![screen-host](https://user-images.githubusercontent.com/6020549/226932565-e91a808d-113d-4802-81b9-aaec2df34d75.jpg)


# IP address resolution by service name   
To find the IP address, you need to know the service name.   
Multiple hosts with the same service name are allowed within the network.   
If you give two nodes the same service name, they can find each other.   
This is useful when doing P2P communication with UDP.   
Write query-service to ESP32#1 and ESP32#2.   
Each mDNS hostname is generated from MAC address.   
Therefore, the other party's mDNS host name is completely unknown.   

### ESP32#1   
This project looks for the service name of ```_service_49876._udp```.   
```
git clone https://github.com/nopnop2002/esp-idf-mdns
cd esp-idf-mdns/query-service
idf.py menuconfig
idf.py flash
```

### ESP32#2   
This project looks for the service name of ```_service_49876._udp```.   
```
cd esp-idf-mdns/query-service
idf.py menuconfig
idf.py flash
```

### Configuration

![config-top](https://user-images.githubusercontent.com/6020549/226929344-8410a99a-545d-4a88-8705-9842d3caf072.jpg)
![config-app-service](https://user-images.githubusercontent.com/6020549/226929361-5775198e-766d-4f77-b54c-99a45b88a544.jpg)


### Screen shot
![screen-service](https://user-images.githubusercontent.com/6020549/226932577-31477732-0770-4def-a1f0-544a6e28b382.jpg)

# Resolving mDNS using ping   

- Edit /etc/nsswitch.conf
```
hosts:          files mdns4_minimal [NOTFOUND=return] dns
```

- Execute ping
```
$ ping {mDNS host name}.local
```


# Resolving mDNS using avahi-utils   

- Install avahi-utils
```
$ sudo apt install avahi-utils
```


- Translate one or more fully qualified host names into addresses.
```
$ avahi-resolve -n esp32-mdns1.local
esp32-mdns1.local       192.168.10.115
```

- Use Avahi daemon to browse all mDNS network services.
```
$ avahi-browse -art
+ enp2s0 IPv4 ESP32 with mDNS                               _service_49876._udp  local
= enp2s0 IPv4 ESP32 with mDNS                               _service_49876._udp  local
   hostname = [esp32-mdns-05C634.local]
   address = [192.168.10.115]
   port = [49876]
   txt = []
```

- Use the Avahi daemon to browse specific mDNS network services.
```
$ avahi-browse -rt _service_49876._udp
+ enp2s0 IPv4 ESP32 with mDNS                               _service_49876._udp  local
= enp2s0 IPv4 ESP32 with mDNS                               _service_49876._udp  local
   hostname = [esp32-mdns-05C634.local]
   address = [192.168.10.115]
   port = [49876]
   txt = []
```

For more information is [here](https://wiki.debian.org/Avahi).
