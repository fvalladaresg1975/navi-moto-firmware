# Navi Moto — firmware

Firmware de **Navi Moto**: una pantalla redonda **Waveshare ESP32-S3 Touch AMOLED 1.75** (466×466) montada en el manillar como **navegador para moto**, al estilo Beeline / BaRing. Funciona junto a la app Android **[navi-moto](https://github.com/fvalladaresg1975/navi-moto)**, que calcula las rutas y le envía todo por Bluetooth LE.

## Funciones

- **Modo ruta** — el camino por delante dibujado sobre la pantalla, caja con la flecha y distancia del próximo giro, velocidad, límite de la vía y aviso rojo al excederlo, distancia restante y anillo de avance hacia el giro.
- **Calles alrededor** — calles de OpenStreetMap tenues bajo la ruta: la app las envía una sola vez y la placa las gira y desplaza según la posición del conductor.
- **Zoom según la velocidad** — cerca en ciudad, lejos en carretera (lo decide la app).
- **Navegación de Google Maps** — indicaciones y flecha de Maps, con velocidad y límite.
- **Salir de la navegación desde la placa**, panel de música y volumen durante la navegación, vuelta automática a la navegación.
- **Notificaciones, llamadas** (contestar / rechazar) y **contactos** (llamar desde la placa).
- **Esferas propias para moto** (dibujadas con LVGL, sin imágenes): *Pixel Moto* (clima, viento, baterías, último viaje), *Tablero* (la hora como tacómetro, batería como medidor de bencina, testigo BT), *Clima de ruta* (próximas 5 horas con probabilidad de lluvia, viento, atardecer), *Copiloto* (hora gigante, alto contraste) y *Foto* (una foto tuya de fondo, elegida y ajustada en la app con «Esfera con foto»).
- **Actualización de firmware por WiFi** desde la app, sin cable: la placa abre su propia red, recibe el .bin en el espacio de firmware libre y solo lo da por bueno cuando vuelve a conectarse al celular; si falla, vuelve sola a la versión anterior.
- **Detección de caídas** con el acelerómetro/giroscopio QMI8658: golpe fuerte + moto tumbada, solo si la moto venía a más de 20 km/h según el GPS del celular (sacar la placa del anclaje estacionado no la dispara) → cuenta regresiva de 30 s con «Estoy bien»; si nadie la cancela, la app envía un SMS con la ubicación a los contactos de emergencia.
- **Batería de la placa y del teléfono** en el panel de control (deslizar hacia abajo).
- **Ahorro de energía** con la pantalla apagada: no se dibuja nada, el panel AMOLED duerme y el procesador baja a 80 MHz; el sensor de caídas se apaga si la detección está desactivada. El puerto serie muestra una línea `[POWER]` por minuto con cuánto se dibujó.
- **Pantalla fluida** (v2.5): el envío al panel corre en el otro núcleo mientras LVGL dibuja; cada cuadro se arma completo en PSRAM y se envía de una vez, sincronizado con el pin TE del panel (GPIO13 en ambas placas), así lo que se desliza no se ve quebrado. Las imágenes escaladas y los arcos del panel de control se guardan ya dibujados.
- **Clima**, calendario, cronómetro, estadísticas del último viaje.
- Interfaz en español, pantalla de inicio con la moto, reloj RTC y batería (PMU AXP2101).

## Compilar y grabar

Con [PlatformIO](https://platformio.org/), según la placa:

| Placa | Entorno |
|---|---|
| ESP32-S3-Touch-AMOLED-1.75 | `lolin_s3_1_75` |
| ESP32-S3-Touch-AMOLED-1.75C (32 MB, sin RTC ni SD; reset de pantalla y táctil en GPIO1 / GPIO2) | `lolin_s3_1_75c` |

```bash
pio run -e lolin_s3_1_75 -t upload
```

Cada firmware lleva grabada su placa («Acerca de» la muestra) y rechaza por WiFi un .bin hecho para la otra. En la 1.75C, sin RTC, la hora se toma del celular al conectarse.

## Protocolo con la app

Sobre el protocolo BLE de la librería [ChronosESP32](https://github.com/fbiego/chronos-esp32), con comandos propios decodificados en `dataCallback` (`hal/esp32/app_hal.cpp`), sin modificar la librería:

| Comando | Sentido | Contenido |
|---|---|---|
| `0xC1` / `0xC2` | placa → app | contestar o rechazar llamada / llamar a un contacto |
| `0xC3` | app → placa | estado de la música |
| `0xC4` | placa → app | salir de la navegación |
| `0xC5` | app → placa | cuadro del modo ruta: camino en píxeles, velocidad, límite y posición del conductor |
| `0xC6` | app → placa | velocidad y límite en modo Google Maps |
| `0xC7` | app → placa | calles alrededor (agregar / borrar) |
| `0xC8` | ambos | brillo de la pantalla: la app lo fija o lo pide; la placa avisa el nivel actual |
| `0xC9` | app → placa | clima extra para las esferas: probabilidad de lluvia por hora, salida y puesta del sol |
| `0xCA` | ambos | foto de la esfera «Foto»: la app envía el JPEG (inicio, datos, fin, quitar); la placa responde el resultado |
| `0xCB` | ambos | actualización por WiFi: la app pide abrir la red de la placa (SSID + clave) o cancelar; la placa responde lista / error / actualizada / volvió atrás. El firmware viaja por TCP (puerto 3232) con su MD5; un .bin de otra placa se rechaza |
| `0xCC` | ambos | detección de caídas: la app la activa, pide una prueba, cancela, informa a cuántos contactos llegó el SMS o envía la velocidad del GPS; la placa avisa cuenta regresiva / cancelada / pedir el SOS |
| `0xCD` | app → placa | la navegación en curso es la ruta de prueba de la app: no se guarda como último viaje |

La batería del teléfono usa el comando propio de la librería (`0x91`).

## Créditos y licencia

Basado en [esp32-c3-mini](https://github.com/fbiego/esp32-c3-mini) de Felix Biego. Licencia MIT — ver [LICENSE](LICENSE). Datos de mapas © colaboradores de OpenStreetMap (ODbL). Tipografía de las esferas: Montserrat (SIL Open Font License).
