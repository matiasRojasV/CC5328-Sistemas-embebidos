# Especificación del Protocolo Tarea 2 (ESP32 - GUI)

## A. Comandos desde la GUI hacia el ESP32 (Texto delimitado sin espacios, igual que en la Tarea 1)

* START\n: Inicia o reanuda la adquisición y transmisión periódica.
* STOP\n: Pausa la adquisición y transmisión.
* SET_ACQ,\<fs>,\<t>,\<h>,\<p>\n: Configura la adquisición.
  * \<fs>: Frecuencia en Hz (0.5, 1.0, 2.0 o 5.0).
  * \<t>,\<h>,\<p>: 1 si la variable está seleccionada para transmitirse, 0 si no (ej. SET_ACQ,2.0,1,0,1\n activa T y P a 2 Hz).
* SET_AVG,<en>,<win_s>\n: Configura el promedio móvil temporal de temperatura.
  * \<en>: 1 (habilitado) o 0 (deshabilitado).
  * \<win_s>: Ventana $W$ en segundos (5, 10, 30 o 60).
* SET_PEAK,\<en>,\<thr_c>\n: Configura la detección de picos de temperatura.
  * \<en>: 1 (habilitado) o 0 (deshabilitado).
  * \<thr_c>: Umbral $\theta$ en °C con paso de 0.1 °C (ej. SET_PEAK,1,28.0\n).

## B. Respuestas y Datos desde el ESP32 hacia la GUI (Precedidos por <{DP}> y terminados en \n)

* **Confirmación de comando válido (ACK):**
<{DP}>ACK,cmd=SET_ACQ,fs=2.0,T=1,H=0,P=1,avg_en=1,win_s=10,peak_en=1,thr_c=28.0\n
* **Rechazo por comando o parámetro inválido (ERR):**
<{DP}>ERR,cmd=SET_ACQ,msg=seleccion_vacia\n (o frecuencia_invalida, ventana_invalida, etc.).
* **Paquete de muestra periódica (SAMPLE):**
<{DP}>SAMPLE,seq=145,t_ms=73210,T=26.40,P=1012.70,temp_avg_c=26.10,win_full=1\n
  * Las Claves T, H y P solo aparecen en el mensaje si estan activas; temp_avg_c y win_full solo aparecen si el promedio esta activo, incluso cuando T "cruda" esta desmarcada
* **Evento asincrónico de "pico" (PEAK):**
<{DP}>PEAK,t_ms=75410,peak_c=28.70,thr_c=28.00\n
