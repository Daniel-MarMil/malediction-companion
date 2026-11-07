<!--
SPDX-FileCopyrightText: 2026 Daniel Martín Millet
SPDX-License-Identifier: Apache-2.0
-->

# Registro de decisiones de arquitectura

Cada decisión sigue el mismo esquema: contexto, decisión, alternativas descartadas y consecuencias. El documento de arquitectura y requisitos es la referencia; estos ficheros acompañan al código.

| Decisión | Título | Estado |
|---|---|---|
| [ADR-01](0001.md) | Núcleo de reglas y protocolo en C++20 compartido | Aceptada |
| [ADR-02](0002.md) | Interfaces nativas sobre el núcleo C++ | Aceptada |
| [ADR-03](0003.md) | Servidor como monolito modular en C++20 con Drogon y PostgreSQL | Aceptada |
| [ADR-04](0004.md) | Partida autoritativa en el servidor por WebSocket | Aceptada |
| [ADR-05](0005.md) | Cuenta con correo verificado; catálogo de cartas sin cuenta | Aceptada |
| [ADR-06](0006.md) | Reglas y cartas como paquetes de datos firmados con Ed25519 | Aceptada |
| [ADR-07](0007.md) | Alojamiento y proveedores solo en el EEE | Propuesta |
| [ADR-08](0008.md) | Licencia Apache 2.0 y dependencias permitidas | Aceptada |
| [ADR-09](0009.md) | Desarrollar con el contenido y la identidad visual oficiales | Aceptada |
| [ADR-10](0010.md) | CMake y vcpkg en modo manifiesto con versiones fijadas | Aceptada |
| [ADR-11](0011.md) | Una única frontera en C entre el núcleo y las apps | Aceptada |
| [ADR-12](0012.md) | Contenido oficial en un paquete privado separado del código | Aceptada |
