# Opened ports on SFR WiFi public hotspots
TCP:
  - 22
  - 25
  - 80
  - 110
  - 143
  - 443
  - 465
  - 587
  - 993
  - 995
  - 5060
  - 8080
  - 8081
  - 8082
  - 9200
  - 9207
  - 9900
  - 10000

UDP:
  - 22
  - 53
  - 500
  - 4500
  - 5060
  - 5228
  - 10000

Source: https://www.justneuf.com/forum/topic/124617-ports-ouverts-sur-sfr-wifi-public/

For this project, it has been determined that using UDP is preferrable, because users will mostly use TCP (HTTP, HTTPS, e-mail protocols...), and TCP over TCP is not a great idea (source: http://sites.inka.de/bigred/devel/tcp-tcp.html).

It has been decided that the network will work on UDP port 5228.
