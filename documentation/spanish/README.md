# Documentación técnica de RoadLink

> **Idioma principal:** Esta es la versión de trabajo principal para la clase. Los cambios se desarrollarán primero en español y después se sincronizarán con la [versión en inglés](../README.md).

Este directorio contiene la documentación técnica de RoadLink. La documentación está dividida por subsistemas para que cada parte pueda estudiarse, probarse y evaluarse de forma independiente. Posteriormente, un documento completo del sistema reunirá estas partes y explicará RoadLink como un conjunto.

## Propósito

RoadLink es un proyecto de comunicación vehicular basado en un sistema embebido. Su controlador central recibe información del vehículo y de los sensores conectados, presenta información útil al usuario y utiliza una conexión celular para transmitir datos fuera del vehículo.

Los documentos de este directorio tienen como objetivo registrar:

- el propósito de cada módulo;
- la razón por la que se seleccionó cada componente;
- la manera en que cada módulo se conecta con el resto de RoadLink;
- las pruebas realizadas durante el desarrollo;
- los cambios entre los primeros prototipos y el diseño actual;
- las limitaciones, los problemas encontrados y las mejoras futuras.

## Mapa de la documentación

| Subsistema | Documento | Estado |
| --- | --- | --- |
| Controlador ESP32 | [Controlador ESP32](modules/controlador-esp32.md) | Borrador inicial |
| Comunicación celular | [Comunicación celular](modules/comunicacion-celular.md) | Borrador inicial |
| Interfaz de bus CAN | `modules/interfaz-bus-can.md` | Planeado |
| GPS | `modules/gps.md` | Planeado |
| Pantalla TFT y encoder rotatorio | `modules/interfaz-humana.md` | Planeado |
| PCB, fuentes de alimentación y conexiones | `hardware/pcb-alimentacion-y-conexiones.md` | Planeado |
| Sistema RoadLink completo | `descripcion-general-roadlink.md` | Planeado para después de los documentos de módulos |

## Organización del material visual

Las fotografías, diagramas, capturas de pantalla y evidencias de pruebas deben guardarse en `assets/images/`. Los nombres de archivo deben describir el elemento y la etapa de desarrollo, por ejemplo:

```text
assets/images/
├── esp32-sim800l-montaje-prueba.jpg
├── sim800l-resultado-prueba-sms.jpg
├── sim800l-montaje-prueba-tcp.jpg
└── esp32-a7670sa-montaje-prueba.jpg
```

Cada imagen debe incluirse en el documento del módulo correspondiente y debe estar acompañada de una descripción breve que explique lo que demuestra. Las imágenes deben respaldar la explicación técnica y no utilizarse únicamente como decoración.

## Estados de la documentación

- **Borrador inicial:** el documento tiene una estructura útil y contenido preliminar, pero todavía requiere mediciones, fotografías o confirmaciones específicas del proyecto.
- **En desarrollo:** se están agregando y comprobando evidencias del proyecto.
- **Verificado:** la descripción se comparó con el hardware final, el firmware, los diagramas eléctricos y los resultados de las pruebas.

## Información que todavía debe confirmarse

Los primeros borradores evitan intencionalmente inventar detalles que todavía no se han registrado. Conforme avance la documentación, se deberá confirmar:

- el modelo exacto de la placa o módulo ESP32 utilizado;
- los pines y puertos seriales asignados a cada periférico;
- los voltajes de alimentación y requisitos de corriente medidos;
- la cronología y los resultados de las pruebas de SMS y TCP/IP con el SIM800L;
- la razón por la que se reemplazó el SIM800L con el A7670SA;
- la configuración del operador, la tarjeta SIM, la antena y la red utilizada durante las pruebas;
- los resultados actuales de las pruebas del A7670SA y su estado de integración final.

