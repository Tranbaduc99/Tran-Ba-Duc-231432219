# Tran-Ba-Duc-231432219

# ESP32 Aquarium Monitoring System

## 1. Giới thiệu
Hệ thống giám sát và điều khiển bể nước sử dụng ESP32.

## 2. Phần cứng
- ESP32 DevKit
- Cảm biến siêu âm HC-SR04
- Cảm biến nhiệt độ DS18B20
- LED mô phỏng máy bơm nước vào
- LED mô phỏng van thoát nước

## 3. Công nghệ sử dụng
- Arduino IDE
- Wokwi Simulator
- MQTT Broker (HiveMQ)
- HTML, CSS, JavaScript
- Chart.js
- FreeRTOS

## 4. Chức năng
- Đo và hiển thị mực nước.
- Đo và hiển thị nhiệt độ.
- Gửi dữ liệu lên MQTT Broker.
- Hiển thị dữ liệu trên Web theo thời gian thực.
- Điều khiển máy bơm và van thoát ở chế độ MANUAL.
- Tự động điều khiển theo mực nước ở chế độ AUTO.

## 5. Cấu trúc dự án
- sketch.ino: Chương trình ESP32.
- diagram.json: Sơ đồ mạch Wokwi.
- libraries.txt: Danh sách thư viện.
- index.html: Giao diện Web.
- README.md: Tài liệu dự án.
