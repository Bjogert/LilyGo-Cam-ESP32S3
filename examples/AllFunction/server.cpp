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

void handle_jpg_stream(void)
{
    WiFiClient client = server.client();
    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
    server.sendContent(response);
    camera_fb_t *fb;
    size_t _jpg_buf_len = 0;
    uint8_t * _jpg_buf = NULL;
    
    while (1) {
        // Feed watchdog timer
        esp_task_wdt_reset();
        
        yield();

        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("fb empty");
            delay(10); // Small delay to prevent tight loop
            continue;
        }
        if (!client.connected()) {
            if (fb) {
                esp_camera_fb_return(fb);
            }
            break;
        }
        
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
        
        response = "--frame\r\n";
        response += "Content-Type: image/jpeg\r\n";
        response += "Content-Length: " + String(_jpg_buf_len) + "\r\n\r\n";
        server.sendContent(response);

        client.write(_jpg_buf, _jpg_buf_len);
        server.sendContent("\r\n");
        
        if (fb) {
            esp_camera_fb_return(fb);
            fb = NULL;
        }
        
        if (!client.connected()) {
            Serial.println("client disconnected!");
            break;
        }
        
        // Small delay to prevent overwhelming the system
        delay(1);
    }
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
    server.on("/", HTTP_GET, handleRoot);
    server.on("/stream", HTTP_GET, handle_jpg_stream);
    server.on("/capture", HTTP_GET, handle_jpg_capture);
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



























