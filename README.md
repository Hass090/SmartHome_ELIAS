git add src/main.cpp
git commit -m "lalalal"
git push origin main

mosquitto_sub -h 127.0.0.1 -t "smarthome/pico/#" -u pico -P 123mqtt456b -v

ssh hass@192.168.1.15

passdb: 123root456maria passphpMA: 123php456admin
