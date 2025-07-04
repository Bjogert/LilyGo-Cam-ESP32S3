/**
 * @file      server.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2022  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2022-09-16
 *
 */

#include "esp_task_wdt.h" // Add watchdog header

#include <WebServer.h>
#include <esp_camera.h>

WebServer server(80);

bool startedServer = false;

// Global variables for dynamic control
int streamDelay = 67; // Default 15 FPS (1000ms / 15 = 67ms)
int currentFPS = 15;
int jpegQuality = 10;
framesize_t currentFrameSize = FRAMESIZE_VGA;

void handle_jpg_stream(void)
{
    WiFiClient client = server.client();
    Serial.println("Stream started");
    
    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
    server.sendContent(response);
    
    camera_fb_t *fb = NULL;
    size_t _jpg_buf_len = 0;
    uint8_t * _jpg_buf = NULL;
    
    while (client.connected()) {
        // Feed watchdog timer
        esp_task_wdt_reset();
        
        // Get frame from camera
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Camera capture failed");
            delay(100); // Longer delay when camera fails
            continue;
        }
        
        // Check if we have valid JPEG data
        if (fb->len == 0) {
            Serial.println("Empty frame buffer");
            esp_camera_fb_return(fb);
            delay(100);
            continue;
        }
        
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
        
        // Send MJPEG boundary and headers
        response = "--frame\r\n";
        response += "Content-Type: image/jpeg\r\n";
        response += "Content-Length: " + String(_jpg_buf_len) + "\r\n\r\n";
        server.sendContent(response);
        
        // Send JPEG data
        client.write(_jpg_buf, _jpg_buf_len);
        server.sendContent("\r\n");
        
        // Return frame buffer
        esp_camera_fb_return(fb);
        fb = NULL;
        
        // Check connection again
        if (!client.connected()) {
            Serial.println("Client disconnected during streaming");
            break;
        }
        
        // Small delay between frames - adjust for desired FPS
        delay(streamDelay); // Dynamic FPS control
    }
    
    Serial.println("Stream ended");
}

void handleRoot()
{
    String html = "<!DOCTYPE html><html><head><title>LilyGo Camera</title></head><body>";
    html += "<h1>LilyGo T-Camera S3</h1>";
    html += "<p>Camera is running!</p>";
    html += "<p><a href=\"/stream\">View Live Stream</a></p>";
    html += "<p><a href=\"/capture\">Capture Single Image</a></p>";
    html += "<img src=\"/stream\" style=\"max-width: 100%; height: auto;\">";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handle_jpg_capture()
{
    camera_fb_t * fb = NULL;
    fb = esp_camera_fb_get();
    if (!fb) {
        server.send(500, "text/plain", "Camera capture failed");
        return;
    }
    
    server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
    server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
}

void handleNotFound()
{
    String message = "Server is running!\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    server.send(200, "text/plain", message);
}


void setupServer()
{
    // Configure server settings for better compatibility
    const char* headerNames[] = {"User-Agent", "Connection", "Accept"};
    server.collectHeaders(headerNames, 3);
    
    server.on("/", HTTP_GET, handleRoot);
    server.on("/stream", HTTP_GET, handle_jpg_stream);
    server.on("/capture", HTTP_GET, handle_jpg_capture);
    
    // FPS Control endpoint
    server.on("/fps", HTTP_GET, []() {
        if (server.hasArg("value")) {
            int newFPS = server.arg("value").toInt();
            if (newFPS >= 1 && newFPS <= 60) {
                currentFPS = newFPS;
                streamDelay = 1000 / newFPS;
                server.send(200, "application/json", "{\"fps\":" + String(currentFPS) + ",\"delay\":" + String(streamDelay) + "}");
            } else {
                server.send(400, "application/json", "{\"error\":\"FPS must be between 1 and 60\"}");
            }
        } else {
            server.send(200, "application/json", "{\"fps\":" + String(currentFPS) + ",\"delay\":" + String(streamDelay) + "}");
        }
    });
    
    // Quality Control endpoint
    server.on("/quality", HTTP_GET, []() {
        if (server.hasArg("value")) {
            int newQuality = server.arg("value").toInt();
            if (newQuality >= 1 && newQuality <= 63) {
                jpegQuality = newQuality;
                sensor_t *sensor = esp_camera_sensor_get();
                if (sensor) {
                    sensor->set_quality(sensor, jpegQuality);
                }
                server.send(200, "application/json", "{\"quality\":" + String(jpegQuality) + "}");
            } else {
                server.send(400, "application/json", "{\"error\":\"Quality must be between 1 and 63\"}");
            }
        } else {
            server.send(200, "application/json", "{\"quality\":" + String(jpegQuality) + "}");
        }
    });
    
    // Resolution Control endpoint
    server.on("/resolution", HTTP_GET, []() {
        if (server.hasArg("value")) {
            String res = server.arg("value");
            framesize_t newSize = FRAMESIZE_VGA;
            
            if (res == "QVGA") newSize = FRAMESIZE_QVGA;
            else if (res == "VGA") newSize = FRAMESIZE_VGA;
            else if (res == "SVGA") newSize = FRAMESIZE_SVGA;
            else if (res == "XGA") newSize = FRAMESIZE_XGA;
            else {
                server.send(400, "application/json", "{\"error\":\"Invalid resolution\"}");
                return;
            }
            
            currentFrameSize = newSize;
            sensor_t *sensor = esp_camera_sensor_get();
            if (sensor) {
                sensor->set_framesize(sensor, currentFrameSize);
            }
            server.send(200, "application/json", "{\"resolution\":\"" + res + "\"}");
        } else {
            String currentRes = "VGA";
            if (currentFrameSize == FRAMESIZE_QVGA) currentRes = "QVGA";
            else if (currentFrameSize == FRAMESIZE_SVGA) currentRes = "SVGA";
            else if (currentFrameSize == FRAMESIZE_XGA) currentRes = "XGA";
            server.send(200, "application/json", "{\"resolution\":\"" + currentRes + "\"}");
        }
    });
    
    server.on("/status", HTTP_GET, []() {
        String status = "{\"status\":\"online\",\"stream\":\"http://";
        status += WiFi.localIP().toString();
        status += "/stream\",\"capture\":\"http://";
        status += WiFi.localIP().toString();
        status += "/capture\",\"fps\":" + String(currentFPS);
        status += ",\"quality\":" + String(jpegQuality);
        status += ",\"resolution\":\"";
        if (currentFrameSize == FRAMESIZE_QVGA) status += "QVGA";
        else if (currentFrameSize == FRAMESIZE_VGA) status += "VGA";
        else if (currentFrameSize == FRAMESIZE_SVGA) status += "SVGA";
        else if (currentFrameSize == FRAMESIZE_XGA) status += "XGA";
        status += "\"}";
        server.send(200, "application/json", status);
    });
    
    server.onNotFound(handleNotFound);
    server.begin();
    startedServer = true;
}


void loopServer()
{
    if (startedServer) {
        server.handleClient();
    }
}



























