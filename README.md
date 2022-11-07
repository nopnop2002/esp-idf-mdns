# esp-idf-mdns
Example of name resolution by mDNS.   

[Here](https://github.com/espressif/esp-idf/tree/master/examples/protocols/sockets) is an official TCP server/client example.   
When communicating over TCP, the client needs the server's port number and IP address.   
However, in a DHCP environment, the server's IP address is not fixed.   
This example uses the mDNS hostname to connect to the server instead of the IP address.   

Since ESP-IDF Ver5, mDNS has been moved from built-in library to component library.   
Accordingly, the mDNS official example has been removed from the ESP-IDF repository.   

You need to add the component using the Manifest File (idf_component.yml) befor build.   
This repository contains a Manifest File (idf_component.yml).   

The official repository for mDNS is [here](https://github.com/espressif/esp-protocols).   

# How to use
Build the server first.   
Then build the client.   
__Both must have the same mDNS hostname and port number.__   
Pressing the Enter key on the client side will shutdown the connection.   
