# WC3-WorldEditor-DarkMode

Modo **UltraDark 100% portátil y exclusivo por proceso** para el **World Editor clásico de Warcraft III** (`1.21–1.27b`) y **Jass NewGen Pack (JNGP)**, sin modificar el tema de Windows ni alterar ningún archivo original del juego o de JNGP.

---

## Características principales

- **Aislado por proceso:** Solo aplica el tema oscuro a la instancia del editor que abras desde el lanzador Dark. Tu Windows y el resto de tus programas permanecen intactos.
- **Convivencia total con el editor original:** Conservas tu `worldedit.exe` y tu `NewGen WE.exe` originales intactos (modo claro), teniendo al lado su respectivo ejecutable `*_Dark.exe`.
- **100% Portátil y ordenado:** Cero dependencias externas ni instaladores. Solo se copia un `.exe` y la subcarpeta `WE_DarkMode\` dentro del directorio correspondiente en cualquier PC.
- **Cobertura visual completa (Classic + JNGP / TESH):**
  - Barras de título oscuras nativas (DWM) en ventana principal, Object Editor, Trigger Editor, Paletas, diálogos y mensajes.
  - Barra de menú superior y menús desplegables oscuros (alineados a la izquierda con atajos a la derecha, separadores y marcas de verificación).
  - Tablas, árboles y sentencias GUI de Triggers (`Configure Event / Condition / Action`) completamente oscuros y legibles.
  - **Soporte nativo TESH / Scintilla (JNGP):** Tema oscuro automático tipo VS Code Dark+ para el editor de código JASS / vJASS (`TESH 0.7` y `TESH 2.0`), incluyendo numeración de líneas, márgenes de plegado, resaltado de sintaxis (`keywords`, `natives`, `types`, `BJs`, comentarios), autocompletado y `calltips`, sin tocar `tesh\config\styles.ini`.
  - Renderizado inteligente de íconos en la barra de herramientas (colores vivos en íconos activos e íconos atenuados nítidos cuando están deshabilitados).
  - **100% visual:** No interfiere con el lienzo 3D Direct3D / OpenGL ni con el guardado de mapas (`.w3x` / `.w3m`), JassHelper, UMSWE o Grimoire.

---

## Versiones disponibles en este repositorio

| Carpeta | Editor objetivo | Ejecutable Dark | Arquitectura | Estado |
| :--- | :--- | :--- | :--- | :--- |
| [`WC3-Classic-1.21-1.27b/`](./WC3-Classic-1.21-1.27b) | **World Editor Clásico (`1.21 – 1.27b`)** | `WorldEditor_Dark.exe` | Win32 (`x86`) | **Listo y estable** |
| [`WC3-JNGP/`](./WC3-JNGP) | **Jass NewGen Pack (`Rebuild`, `2.0.x`, `1.4g`, `5d`)** | `NewGen_WE_Dark.exe` | Win32 (`x86`) | **Listo y estable** |

---

## Instalación rápida (Portátil)

### 1. Para World Editor Clásico (`1.21 – 1.27b`)

Copia el contenido de [`WC3-Classic-1.21-1.27b`](./WC3-Classic-1.21-1.27b) dentro de la carpeta raíz de tu **Warcraft III**:

```text
<Carpeta de Warcraft III>\
├── worldedit.exe              <-- Editor clásico normal (intacto)
├── WorldEditor_Dark.exe       <-- Ábrelo para usar el Modo Oscuro
└── WE_DarkMode\
    ├── WE_DarkMode.dll        <-- Motor UltraDark (32-bit, sin dependencias)
    └── WE_DarkMode.ini        <-- Paleta de colores editable
```

### 2. Para Jass NewGen Pack (`JNGP Rebuild` / `2.0.x` / `5d`)

Copia el contenido de [`WC3-JNGP`](./WC3-JNGP) dentro de la carpeta raíz de tu **Jass NewGen Pack** (junto a `NewGen WE.exe`):

```text
<Carpeta de Jass NewGen Pack>\
├── NewGen WE.exe              <-- Lanzador JNGP original (intacto)
├── NewGen_WE_Dark.exe         <-- Ábrelo para usar JNGP + TESH en Modo Oscuro
└── WE_DarkMode\
    ├── WE_DarkMode_JNGP.dll   <-- Motor UltraDark + TESH/Scintilla (32-bit)
    └── WE_DarkMode.ini        <-- Paleta de colores editable
```

*(En ambos lanzadores también puedes arrastrar un mapa `.w3x` o `.w3m` encima del `.exe` para abrirlo directamente en modo oscuro).*

---

## Personalización de colores (`WE_DarkMode.ini`)

Puedes ajustar todos los colores en formato hexadecimal `RRGGBB` editando `WE_DarkMode\WE_DarkMode.ini`:

```ini
[General]
; Vacío = detección automática en la misma carpeta
WorldEditPath=
Arguments=

[Colors]
BgColor=181818
SurfaceColor=1E1E1E
InputColor=121212
TextColor=EAEAEA
MutedTextColor=8A8A8A
AccentColor=264F78
HoverColor=2E2E32
BorderColor=383838
```

---

## Compilar desde el código fuente

Requisitos: **MSYS2 MinGW-w64 32-bit** (`mingw-w64-i686-gcc` y `windres` en `C:\msys64\mingw32\bin\`).

```powershell
.\build.ps1
```

Genera binarios estáticos de 32 bits (`WE_DarkMode.dll`, `WorldEditor_Dark.exe` y `NewGen_WE_Dark.exe`) enlazados únicamente contra DLLs nativas de Windows y empaqueta ambas carpetas de distribución.

---

## Licencia

Distribuido bajo la licencia [MIT](./LICENSE).
