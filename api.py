import time
import mysql.connector

from library.credentials import *
from library.common import *

from flask import Flask
from flask import request
from flask import json
from flask import Response

from library.MQTT import fetch_mqtt_msg

app = Flask(__name__)

@app.route('/linky/inst', methods = ['GET'])
def api_linky_inst():

    publish_topic = "esp32/output"
    listen_topics  = ["esp32/Papp_i", "esp32/I_i", "esp32/Conso_i"]
    data = {}

    for topic in listen_topics:
        response = fetch_mqtt_msg(mqtt_auth, mqtt_server, publish_topic, topic)
	data.update({topic: response})
	time.sleep(1)

    js = json.dumps(data)

    resp = Response(js, status=200, mimetype='application/json')
    return resp

@app.route('/linky/moy', methods = ['GET'])
def api_linky_moy():

    publish_topic = "esp32/output"
    listen_topics  = ["esp32/Papp_m", "esp32/I_m"]
    data = {}

    for topic in listen_topics:
        response = fetch_mqtt_msg(mqtt_auth, mqtt_server, publish_topic, topic)
        data.update({topic: response})
	time.sleep(1)

    js = json.dumps(data)

    resp = Response(js, status=200, mimetype='application/json')
    return resp

if __name__ == '__main__':
    app.run(host="0.0.0.0", debug=True) #host="0.0.0.0"
