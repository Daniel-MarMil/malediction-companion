<!--
SPDX-FileCopyrightText: 2026 Daniel Martín Millet
SPDX-License-Identifier: Apache-2.0
-->

# Contenido oficial de Malediction (privado)

Esta carpeta guarda, solo en el equipo de desarrollo, el paquete con el contenido oficial de
Malediction: nombres, textos, estadísticas e imágenes de las cartas, y el tema visual con las
tipografías, los colores y los iconos oficiales (RF-40 y RNF-22).

Git ignora todo lo que hay aquí salvo este fichero. **No lo subas al repositorio, a Codeberg,
a la integración continua ni a las tiendas.** Ese contenido pertenece a Loot Studios y sus
condiciones de uso prohíben reproducirlo o distribuirlo sin permiso expreso, también sin
ánimo de lucro. El proyecto se desarrolla como si tuviera licencia (ADR-09), pero nada oficial
sale del equipo de desarrollo hasta tener una licencia escrita (ADR-12).

## Qué va aquí

Estructura prevista; el núcleo ya lee el paquete de reglas y el tema llegará en la fase 1:

```text
content/official/
├── rules-pack.json        Paquete de reglas con las cartas oficiales (formato mc-rules-pack)
├── rules-pack.json.sig    Firma Ed25519 separada del paquete (64 bytes, ADR-06)
├── images/                Imágenes de las cartas, una por identificador de carta
└── theme/
    ├── theme.json         Colores, tipografías e iconos del tema oficial
    ├── fonts/             Tipografías oficiales, con licencia para incrustarlas en una app
    └── icons/             Iconos oficiales: sigilos, eco, maestría
```

El formato de `rules-pack.json` es el mismo que el del paquete ficticio de
[`content/test/rules-pack.test.json`](../test/rules-pack.test.json).

## De dónde sale

- **Cartas e imágenes:** de la biblioteca oficial de cartas, solo para el desarrollo.
- **Tipografías, colores e iconos:** no hay una guía de marca pública. Loot Studios ofrece un
  kit de marca a las tiendas y hay que pedírselo (suporte@malediction.gg). Cada tipografía
  necesita además una licencia que permita incrustarla en una app.
- **Accesibilidad:** los colores oficiales se usan donde cumplan el contraste de RNF-13; si no,
  solo como acento.

Sin esta carpeta, la app usa un tema neutro y las fichas propias de RF-03.
