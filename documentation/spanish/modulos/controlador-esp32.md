# Controlador ESP32

**Estado del documento:** Borrador inicial  
**Subsistema:** Controlador principal y plataforma de firmware

## 1. Función dentro de RoadLink

El ESP32 es el controlador principal, o “cerebro”, de RoadLink. Coordina los demás módulos electrónicos y ejecuta el firmware que define el comportamiento del sistema. Entre sus responsabilidades se encuentran recopilar información, procesarla, actualizar la interfaz de usuario y solicitar al módulo celular que transmita datos a un destino externo.

Dentro del sistema completo, se espera que el ESP32 interactúe con:

- la interfaz de bus CAN, que proporciona información proveniente del vehículo;
- el receptor GPS, que proporciona datos de posición y movimiento;
- el módulo celular, que proporciona comunicación de larga distancia;
- la pantalla TFT, que presenta información al usuario;
- el encoder rotatorio y su botón, que reciben las entradas del usuario;
- los circuitos de alimentación y acondicionamiento de señales de la PCB.

El ESP32 no realiza todas las funciones por sí solo. En cambio, actúa como coordinador entre módulos especializados. Esta separación permite probar un periférico de forma independiente antes de integrarlo al sistema RoadLink completo.

## 2. Razones para utilizar un ESP32

La familia ESP32 es adecuada para este tipo de prototipo porque combina un microcontrolador programable con varias interfaces de comunicación. RoadLink puede utilizar estos periféricos de hardware para comunicarse con múltiples módulos sin necesitar un procesador diferente para cada uno.

Entre sus capacidades relevantes se encuentran:

- comunicación serial UART para los módulos celular y GPS;
- comunicación SPI para dispositivos como la pantalla TFT y el controlador CAN;
- pines GPIO para botones, señales del encoder, líneas de reinicio e indicadores de estado;
- temporizadores e interrupciones para responder a las entradas y ejecutar tareas periódicas;
- capacidad de procesamiento suficiente para interpretar mensajes y controlar varios subsistemas;
- memoria flash no volátil para almacenar el firmware y la configuración de RoadLink.

El modelo exacto de placa o módulo ESP32 y las interfaces utilizadas en el prototipo final se agregarán después de compararlos con el hardware y el firmware.

## 3. Posición dentro del sistema

El siguiente diagrama lógico muestra la función central del ESP32. Representa el flujo de información, no las asignaciones finales de pines.

```text
                    ┌───────────────────┐
Bus CAN del vehículo► Interfaz de bus CAN──┐
                    └───────────────────┘  │
                                           ▼
┌──────────────┐                     ┌───────────┐                    ┌────────────────┐
│ Receptor GPS │────────────────────►│   ESP32   │◄──────────────────►│ Módulo celular │
└──────────────┘                     │controlador│                    └────────────────┘
                                     └─────┬─────┘
                                           │
                          ┌────────────────┴──────────────┐
                          ▼                               ▼
                   ┌─────────────┐                ┌────────────────┐
                   │Pantalla TFT │                │Encoder rotatorio│
                   └─────────────┘                └────────────────┘
```

Las conexiones de alimentación y los dominios de voltaje no aparecen aquí porque se documentarán en el apartado de PCB y fuentes de alimentación.

## 4. Responsabilidades del firmware

El firmware debe organizar las tareas de comunicación y de interfaz de usuario de manera que una operación lenta en un módulo no detenga todo el sistema. De forma general, su ciclo de operación es:

1. inicializar el ESP32 y los módulos conectados;
2. comprobar si cada módulo requerido responde;
3. recibir datos del vehículo y del GPS;
4. validar y almacenar los valores útiles más recientes;
5. actualizar la pantalla y responder a las entradas del encoder;
6. preparar la información que debe transmitirse;
7. intercambiar comandos y datos con el módulo celular;
8. detectar errores de comunicación y reintentar o reportarlos de manera segura.

Cuando el código se agregue al repositorio, el documento final deberá identificar los archivos o funciones del firmware responsables de estas tareas.

## 5. Historial de desarrollo y pruebas

El ESP32 se probó inicialmente con el módulo celular SIM800L antes de integrar por completo el resto de RoadLink. Esto redujo la cantidad de variables involucradas y permitió desarrollar por separado la comunicación serial y el proceso de transmisión de datos.

La secuencia de pruebas recordada actualmente es:

1. conectar el ESP32 y el SIM800L como un montaje de prueba independiente;
2. utilizar datos simulados de RoadLink en lugar de datos reales del vehículo;
3. enviar la información simulada mediante SMS;
4. realizar posteriormente una prueba de transmisión de datos por red, que se cree que utilizó TCP/IP;
5. reemplazar el SIM800L con el A7670SA para la versión posterior del proyecto.

Esta secuencia deberá compararse con los programas originales, las fotografías, los mensajes o los registros de prueba antes de marcarla como verificada. Los resultados deben indicar qué funcionó, qué falló y qué cambios fueron necesarios, no solamente que se intentó una prueba.

## 6. Interfaces que deben documentarse

| Subsistema conectado | Interfaz probable | Información intercambiada | Detalles pendientes |
| --- | --- | --- | --- |
| Módulo celular | UART | Comandos AT, respuestas y datos útiles | Número de UART, pines TX/RX, velocidad en baudios y pines de control |
| Receptor GPS | UART | Posición, hora, velocidad y estado | Modelo del módulo, número de UART, pines y velocidad en baudios |
| Interfaz de bus CAN | SPI o ruta CAN/TWAI integrada | Tramas del vehículo e información de estado | Modelos del controlador y transceptor, además de los pines |
| Pantalla TFT | SPI o interfaz específica de la pantalla | Gráficos, texto y estados | Modelo, pines, resolución y biblioteca utilizada |
| Encoder rotatorio | GPIO | Eventos de giro y del botón | Pines, resistencias pull-up y método de eliminación de rebote |

Se utiliza el término “interfaz probable” hasta que cada conexión se verifique con el diseño real de RoadLink.

## 7. Evidencias de prueba que deben agregarse

La sección del ESP32 será más sólida si incluye evidencias reproducibles. Algunas adiciones útiles son:

- una fotografía clara del cableado de prueba entre el ESP32 y el SIM800L;
- una fotografía clara del cableado de prueba entre el ESP32 y el A7670SA;
- el paquete o mensaje exacto de datos simulados;
- la salida del monitor serial que muestre los comandos y las respuestas;
- el SMS recibido o los datos observados en el servidor;
- la versión del firmware utilizada en cada prueba;
- una tabla con las condiciones, los resultados esperados y los resultados reales.

Tabla de pruebas sugerida:

| Prueba | Configuración | Resultado esperado | Resultado real | Evidencia | Estado |
| --- | --- | --- | --- | --- | --- |
| Respuesta serial del SIM800L | ESP32 + SIM800L | El módulo responde a un comando AT | Pendiente | Salida serial/fotografía | No verificado |
| Transmisión por SMS | ESP32 + SIM800L + datos simulados | El teléfono de prueba recibe los datos con el formato esperado | Pendiente | Captura del SMS | No verificado |
| Transmisión TCP/IP | ESP32 + SIM800L + datos simulados | El destino remoto recibe la carga útil | Pendiente de confirmar | Registro/captura | No verificado |
| Comunicación con A7670SA | ESP32 + A7670SA | El módulo se registra e intercambia datos | Pendiente | Registro serial/de red | No verificado |

## 8. Consideraciones y limitaciones del diseño

- El tráfico UART debe interpretarse sin confundir respuestas normales, errores y mensajes no solicitados del módem.
- Las operaciones celulares pueden tardar más que las operaciones locales del microcontrolador, por lo que se necesitan tiempos límite y límites de reintentos.
- El ESP32 y todos los periféricos deben utilizar niveles lógicos compatibles o circuitos adecuados de adaptación de nivel.
- El controlador debe seguir respondiendo a la pantalla y al encoder mientras espera información del GPS, CAN o módulo celular.
- Las fallas de los módulos deben producir información de diagnóstico útil en lugar de detener todo el firmware.
- La asignación de pines debe considerar los pines con restricciones durante el arranque y las interfaces compartidas por varios dispositivos.

## 9. Información necesaria para completar este documento

- nombre y revisión de la placa o módulo ESP32;
- entorno de desarrollo y bibliotecas importantes;
- arquitectura actual del firmware;
- tabla final de asignación de pines;
- secuencia de arranque y orden de inicialización de los módulos;
- comportamiento ante errores y procedimiento de recuperación;
- mediciones de consumo eléctrico;
- fotografías con fecha y resultados de pruebas verificados.

