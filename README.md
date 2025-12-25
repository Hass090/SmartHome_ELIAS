git add src/main.cpp
git commit -m "lalalal"
git push origin main

mosquitto_sub -h 127.0.0.1 -t "smarthome/pico/#" -v -u pico_user -P 123mqtt456b
