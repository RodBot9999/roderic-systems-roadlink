# Subsistema de comunicación celular

**Estado del documento:** Borrador inicial  
**Versiones del subsistema:** SIM800L (primer prototipo) y A7670SA (versión posterior)

## 1. Propósito

El subsistema celular permite que RoadLink se comunique fuera del vehículo. El ESP32 prepara la información que debe enviarse, mientras que el módulo celular se encarga de la comunicación con la red móvil. Al separar estas responsabilidades, el controlador puede concentrarse en la lógica de RoadLink y el módem puede encargarse del registro en la red y de la transmisión.

Durante el desarrollo, la comunicación celular se probó de manera independiente utilizando datos simulados. Esto fue importante porque permitió evaluar el método de transmisión antes de que la integración del bus CAN, el GPS, la pantalla y la interfaz de usuario introdujera variables adicionales.

## 2. Evolución durante el desarrollo

RoadLink ha utilizado dos etapas de módulos celulares:

1. **Prototipo con SIM800L.** La primera versión utilizó un ESP32 conectado a un SIM800L. Inicialmente se transmitieron datos simulados de RoadLink mediante SMS. Se cree que una prueba posterior cambió de SMS a una conexión de datos TCP/IP.
2. **Versión con A7670SA.** Posteriormente, el proyecto cambió a un módulo A7670SA. Este se convirtió en la plataforma celular del diseño más reciente de RoadLink.

Todavía es necesario recuperar las fechas exactas, los resultados de las pruebas y la razón técnica del cambio a partir del código original, las fotografías y los registros de prueba. Hasta que se agreguen esas evidencias, la secuencia anterior deberá considerarse un historial preliminar del proyecto.

## 3. Primera etapa: SIM800L

### 3.1 Función dentro del primer prototipo

El SIM800L proporcionó una manera práctica de demostrar que el ESP32 podía controlar un módem celular mediante comandos seriales. El montaje de prueba se limitó intencionalmente al ESP32 y al módulo celular, mientras que valores simulados reemplazaron la información que posteriormente provendría de los demás subsistemas de RoadLink.

Este enfoque permitió probar varias ideas esenciales:

- comunicación serial entre el ESP32 y el módem;
- control del módem mediante comandos AT;
- acceso a la tarjeta SIM y a la red móvil;
- conversión de valores internos en un mensaje que pudiera transmitirse;
- confirmación de que la información llegó a un destino externo.

### 3.2 Prueba mediante SMS

En el primer método de transmisión, el ESP32 dio formato de mensaje de texto a los datos simulados e indicó al SIM800L que los enviara mediante SMS. El SMS fue útil como primera prueba de concepto porque el propio mensaje recibido proporcionó una confirmación visible de la transmisión de extremo a extremo.

El registro final de esta prueba deberá incluir:

- los valores simulados exactos y el formato del mensaje;
- la secuencia esencial de comandos y respuestas;
- si el mensaje llegó correctamente y cuánto tiempo tardó;
- cualquier problema de registro, señal, antena, tarjeta SIM o alimentación;
- una captura de pantalla o fotografía del SMS recibido;
- el firmware utilizado para la prueba.

### 3.3 Prueba mediante TCP/IP

Después de la etapa de SMS, se cree que el desarrollo avanzó hacia una prueba de transmisión basada en TCP/IP. A diferencia del SMS, este método puede enviar datos de la aplicación por medio de una conexión de paquetes hacia un servicio remoto. Se aproxima más al método de comunicación normalmente requerido para telemetría continua o estructurada.

Esta parte del historial necesita confirmación. La documentación deberá identificar:

- si se utilizó TCP o UDP;
- la configuración del nombre del punto de acceso (APN), sin incluir credenciales privadas;
- el tipo de destino, como un servidor de prueba o un servicio en la nube;
- el formato de la carga útil;
- la manera en que se verificó una conexión y entrega exitosa;
- el comportamiento después de un tiempo de espera, una desconexión o un reinicio del módem.

No se debe publicar en el repositorio ninguna dirección de servidor privada, identificador de SIM, número telefónico, contraseña o clave privada de API.

## 4. Segunda etapa: A7670SA

El A7670SA reemplazó al SIM800L en el diseño posterior de RoadLink. Aunque ambos módulos se controlan mediante una interfaz serial basada en comandos AT, no deben considerarse intercambiables. Los comandos, las capacidades de red, los requisitos de alimentación, las indicaciones de estado, el proceso de inicialización y la compatibilidad con bibliotecas pueden ser diferentes.

Por lo tanto, la migración requiere verificación en varias áreas:

- configuración de UART y pines de control del ESP32;
- secuencia de encendido y reinicio del módulo;
- detección de la tarjeta SIM y registro en la red;
- conexión de la antena y comprobaciones de calidad de señal;
- contexto de datos por paquetes y configuración de red;
- comandos de sesión de datos e interpretación de respuestas;
- recuperación después de una pérdida de red o inestabilidad de alimentación.

La razón específica por la que se seleccionó el A7670SA debe documentarse de acuerdo con la decisión real del proyecto. Las posibles consideraciones no deben presentarse como hechos hasta que sean confirmadas.

## 5. Comunicación con el ESP32

En el nivel lógico, las dos versiones celulares utilizan el mismo tipo de intercambio:

```text
┌────────────────────────┐                       ┌─────────────────────────┐
│         ESP32          │                       │      Módulo celular     │
│                        │  Comando AT / datos   │   SIM800L o A7670SA     │
│ Preparar datos         │──────────────────────►│                         │
│ Interpretar respuestas│◄──────────────────────│ Respuesta de red/estado │
│ Aplicar límites/reint. │   Enlace serial UART  │ Transmitir por la red   │
└────────────────────────┘                       └────────────┬────────────┘
                                                             │
                                                             ▼
                                                   Red móvil / destino
                                                          remoto
```

El diagrama final de cableado deberá mostrar, como mínimo, TX, RX, tierra, alimentación del módulo y cualquier pin de encendido, reinicio o estado que realmente se utilice. También deberá distinguir entre la alimentación principal del módem y el nivel lógico de su UART.

## 6. Importancia de la fuente de alimentación

Los transmisores celulares pueden producir cambios rápidos en la demanda de corriente. Una fuente que parece funcionar correctamente cuando se mide sin realizar una transmisión puede presentar caídas de voltaje o ruido cuando el módem se conecta a la red o transmite. Los síntomas pueden incluir reinicios aleatorios, fallas de registro, mensajes incompletos o respuestas seriales que se interrumpen inesperadamente.

Por este motivo, las pruebas celulares deben registrar:

- el voltaje de alimentación del módulo en reposo y durante la transmisión;
- la capacidad de la fuente y el regulador utilizado;
- la configuración de las conexiones a tierra;
- los capacitores de desacoplamiento o de reserva local;
- el comportamiento de reinicio durante la actividad de red.

Los valores exactos aceptables de voltaje y corriente deben obtenerse de la hoja de datos de la placa específica utilizada, no suponerse a partir del nombre de la familia del módem. El diseño completo de alimentación se explicará en el documento de PCB y fuentes de alimentación.

## 7. Registro comparativo

Esta tabla separa el historial confirmado del proyecto de los detalles que todavía requieren evidencia.

| Tema | Prototipo SIM800L | Versión A7670SA |
| --- | --- | --- |
| Etapa del proyecto | Primer prototipo celular | Selección celular posterior/actual |
| Controlador | ESP32 | ESP32 |
| Fuente inicial de datos | Datos simulados de RoadLink | Pendiente de documentar |
| Método demostrado | SMS | Pendiente de documentar |
| Método de datos posterior | Se cree que se probó TCP/IP | Pendiente de documentar |
| Interfaz física | Se espera UART; verificar cableado | Se espera UART; verificar cableado |
| Evidencia de prueba | Se requieren fotografías, código, SMS y registros | Se requieren fotografías, código y registros |
| Razón de la migración | Todavía no documentada | Todavía no documentada |

## 8. Procedimiento de prueba repetible propuesto

El siguiente procedimiento puede adaptarse para ambos módulos:

1. inspeccionar todo el cableado y confirmar los niveles de alimentación y lógica requeridos;
2. alimentar el módem con la fuente prevista mientras se monitorea su voltaje;
3. abrir el registro serial del ESP32 y confirmar la comunicación básica mediante comandos AT;
4. verificar la detección de la tarjeta SIM;
5. esperar el registro en la red y anotar el resultado;
6. consultar y registrar la calidad de señal;
7. configurar el servicio de SMS o de datos por paquetes seleccionado;
8. transmitir una carga útil conocida con una marca de tiempo o un número de secuencia;
9. verificar la recepción en el destino;
10. repetir la prueba y registrar fallas, retrasos, reinicios y el comportamiento de recuperación.

| Campo | Valor que se debe registrar |
| --- | --- |
| Fecha y versión del firmware | Pendiente |
| Módulo celular y revisión de la placa | Pendiente |
| SIM/operador | Pendiente, sin identificadores privados |
| Antena | Pendiente |
| Fuente y voltaje medido | Pendiente |
| Resultado de calidad de señal | Pendiente |
| Carga útil | Pendiente |
| Resultado esperado | Pendiente |
| Resultado real | Pendiente |
| Tiempo de entrega | Pendiente |
| Aprobado/reprobado y observaciones | Pendiente |

## 9. Imágenes y evidencias que deben agregarse

Cuando las fotografías estén disponibles, deberán agregarse a `documentation/spanish/assets/images/` y colocarse cerca de la descripción de la prueba correspondiente. Algunas imágenes útiles son:

- ESP32 únicamente con el SIM800L, mostrando el cableado de prueba;
- el SMS recibido que contiene los datos simulados;
- evidencia de la prueba de datos por paquetes del SIM800L;
- ESP32 únicamente con el A7670SA, mostrando el cableado de la prueba posterior;
- salida serial del registro, calidad de señal y transmisión;
- un diagrama de cableado con etiquetas, creado a partir de las fotografías y verificado con el circuito.

Cada fotografía debe incluir una descripción que indique el módulo, la etapa de desarrollo y el propósito del montaje. Antes de su publicación, se deben eliminar los números telefónicos, identificadores de SIM, credenciales o destinos privados que sean visibles.

## 10. Información necesaria para completar este documento

- versiones exactas de las placas SIM800L y A7670SA;
- orden cronológico y fechas de las pruebas;
- resultado confirmado de la prueba por SMS;
- confirmación de la prueba TCP/IP y del protocolo utilizado;
- ejemplos de cargas útiles sin datos privados;
- método final de transmisión y resultado de las pruebas del A7670SA;
- velocidades de UART, pines del ESP32 y pines de control del módem;
- antenas, operador/red y configuración relevante;
- voltaje de alimentación medido y comportamiento durante la transmisión;
- razón real del cambio de módulos;
- fotografías, firmware y registros seriales.

