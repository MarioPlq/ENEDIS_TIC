sudo mkdir /var/run/mosquitto/
sudo touch /var/run/mosquitto/mosquitto.pid
sudo chmod 666 /var/lib/mosquitto/mosquitto.db
sudo chmod 666 /var/log/mosquitto/mosquitto.log
sudo chmod 666 /var/run/mosquitto/mosquitto.pid
sudo systemctl stop mosquitto.service
sudo systemctl start mosquitto.service
