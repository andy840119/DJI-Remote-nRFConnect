# Third-Party Notices

This project, **DJI-Remote-nRFConnect**, includes or interacts with third-party components that are
licensed under terms different from the project's main MIT License.  
This document provides attribution and license references for those components.


# 1. DJI-Remote (upstream project)

This project is a port of **DJI-Remote** by Rhönschrat:

https://github.com/rhoenschrat/DJI-Remote

Most of the source in this repository is taken from that project and carries
its copyright. It is offered under the **MIT License**, reproduced in
`LICENSE`.


# 2. DJI R SDK Protocol

The DJI R SDK Protocol (including but not limited to the documents  
`protocol.md`, `protocol_CN.md`, `protocol_data_segment.md`, and  
`protocol_data_segment_CN.md`) is licensed under the **DJI End User License Agreement (EULA)**.

These protocol documents **are not covered** by the MIT License of this project.

**DJI EULA:**  
http://developer.dji.com/policies/eula/


# 3. DJI Osmo GPS Controller Demo

Portions of this project are derived from or inspired by the  
**Osmo GPS Controller Demo** published by SZ DJI Technology Co., Ltd.

The demo is offered under:

### DJI EULA  
http://developer.dji.com/policies/eula/

### MIT License
```
The MIT License (MIT)
Copyright (c) 2025 SZ DJI Technology Co., Ltd.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

**Full MIT License text:**  
https://opensource.org/licenses/MIT

The MIT terms above apply only to portions originating from DJI's MIT-licensed demo,  
and **not** to DJI EULA–restricted components.


# 4. Zephyr RTOS / nRF Connect SDK

This firmware is built on the **Zephyr RTOS** and Nordic Semiconductor's
**nRF Connect SDK**, which replace ESP-IDF from the original project.

Both are licensed under the **Apache License 2.0**.

**Source:**
https://github.com/nrfconnect/sdk-nrf
https://github.com/zephyrproject-rtos/zephyr

**Apache 2.0 License:**
https://www.apache.org/licenses/LICENSE-2.0


# 5. Nordic SoftDevice Controller

The Bluetooth LE controller comes from **nrfxlib** and is distributed under the
**Nordic 5-Clause License**.

**Source:**
https://github.com/nrfconnect/sdk-nrfxlib


# 6. LVGL v9.5.0

This project uses **LVGL** for all UI rendering.

LVGL is licensed under the **MIT License**.

**Source:**
https://github.com/lvgl/lvgl

**MIT License:**
https://opensource.org/licenses/MIT


# 7. Summary

- The **DJI-Remote project code**, which this port is derived from, is licensed under **MIT**.
- DJI SDK **protocol files** are under **DJI EULA** and not MIT-licensed.
- Code borrowed from **DJI's GPS Controller Demo** may be under **MIT** or **DJI EULA** depending on the component.
- **Zephyr** and the **nRF Connect SDK** are under **Apache 2.0**.
- The **Nordic SoftDevice Controller** is under the **Nordic 5-Clause License**.
- **LVGL v9.5.0** is under **MIT**.

The ESP-IDF, esp_lvgl_port and Apache NimBLE notices from the original project
no longer apply: this port does not use them.

This notice ensures license clarity and proper attribution for all included third-party resources.
