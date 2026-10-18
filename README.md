# Navi Moto — firmware

Firmware de **Navi Moto**: una pantalla redonda **Waveshare ESP32-S3 Touch AMOLED 1.75** (466×466) montada en el manillar como **navegador para moto**, al estilo Beeline / BaRing. Funciona junto a la app Android **[navi-moto](https://github.com/fvalladaresg1975/navi-moto)**, que calcula las rutas y le envía todo por Bluetooth LE.

## Funciones

- **Modo ruta** — el camino por delante dibujado sobre la pantalla, caja con la flecha y distancia del próximo giro, velocidad, límite de la vía y aviso rojo al excederlo, distancia restante y anillo de avance hacia el giro.
- **Calles alrededor** — calles de OpenStreetMap tenues bajo la ruta: la app las envía una sola vez y la placa las gira y desplaza según la posición del conductor.
- **Zoom según la velocidad** — cerca en ciudad, lejos en carretera (lo decide la app).
- **Navegación de Google Maps** — indicaciones y flecha de Maps, con velocidad y límite.
- **Salir de la navegación desde la placa**, panel de música y volumen durante la navegación, vuelta automática a la navegación.
- **Notificaciones, llamadas** (contestar / rechazar) y **contactos** (llamar desde la placa).
- **Clima**, calendario, cronómetro, estadísticas del último viaje.
- Interfaz en español, pantalla de inicio con la moto, reloj RTC y batería (PMU AXP2101).

## Compilar y grabar

Con [PlatformIO](https://platformio.org/), entorno `lolin_s3_1_75`:

```bash
pio run -e lolin_s3_1_75 -t upload
```

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

## Créditos y licencia

Basado en [esp32-c3-mini](https://github.com/fbiego/esp32-c3-mini) de Felix Biego. Licencia MIT — ver [LICENSE](LICENSE). Datos de mapas © colaboradores de OpenStreetMap (ODbL).
