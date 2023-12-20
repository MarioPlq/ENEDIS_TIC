import mysql.connector

from library.common import *
from library.credentials import *

response = requests.get("http://localhost:5000/linky")
response = response.json()

now = datetime.datetime.now()

#send data
conn = mysql.connector.connect(host=mysql_host, user=mysql_user, password=mysql_psw, database=mysql_database)
cursor = conn.cursor()   
values = (None, datetime.datetime(now.year, now.month, now.day, now.hour, now.minute), 
				response['esp32/Papp_i'], response['esp32/Papp_m'], 
				response['esp32/Conso_i'])
cursor.execute("""INSERT INTO data_linky (id, datetime, Papp_i, Papp_m, Conso_i) VALUES (%s, %s, %s, %s, %s)""", values)
conn.commit()
conn.close()
