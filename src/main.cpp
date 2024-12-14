#include <algorithm>

#include <Arduino.h>
#include <HttpClient.h>
#include <WiFi.h>
#include <inttypes.h>
#include <stdio.h>
#include <time.h>

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

char ssid[] = "Velop";
char pass[] = "9e9ea676";

const int interval_delay = 20;

// Number of milliseconds to wait without receiving any data before we give up
const int kNetworkTimeout = 30 * 1000;
// Number of milliseconds to wait if no data is available before trying again
const int kNetworkDelay = 1000;

const int PULSE_SENSOR_PIN = 33;

// Variables for measuring heart rate
int threshold = 300;                     // increase between two points needed
int sig = 0;                             // electrical signal
const int peaks_queue_len = 10;          // number of items to be stored in queue
int recent_peaks[peaks_queue_len] = {0}; // stored recent peak times in queue
int recent_peaks_pos = 0;                // current position of the queue's pointer

// Variables for detecting peaks
const int min_height = 2750;      // minimum height required to be a "peak"
const int prev_sig_len = 20;      // number of items to be stored in queue
int prev_sig[prev_sig_len] = {0}; // previous sig data stored
int prev_sig_pos = 0;             // current position of the queue's pointer

// Last time heart rate calculated
int last_calculation = millis();
// Interval of 5 seconds for posting data
const int POST_INTERVAL = 5000;

void setup()
{
  Serial.begin(9600);

  // Connect to WiFi
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("MAC address: ");
  Serial.println(WiFi.macAddress());

  WiFiClient c;
  HttpClient http(c);

  int err = http.get("35.160.204.3", 3001, "/get_data", NULL);

  if (err == 0)
  {
    Serial.println("startedRequest ok");
    err = http.responseStatusCode();
    if (err >= 0)
    {
      Serial.print("Got status code: ");
      Serial.println(err);

      // Usually you'd check that the response code is 200 or a
      // similar "success" code (200-299) before carrying on,
      // but we'll print out whatever response we get

      err = http.skipResponseHeaders();
      if (err >= 0)
      {
        int bodyLen = http.contentLength();
        Serial.print("Content length is: ");
        Serial.println(bodyLen);
        Serial.println();
        Serial.println("Body returned follows:");

        // Now we've got to the body, so we can print it out
        unsigned long timeoutStart = millis();
        char c;
        // Whilst we haven't timed out & haven't reached the end of the body
        while ((http.connected() || http.available()) &&
               ((millis() - timeoutStart) < kNetworkTimeout))
        {
          if (http.available())
          {
            c = http.read();
            // Print out this character
            Serial.print(c);

            bodyLen--;
            // We read something, reset the timeout counter
            timeoutStart = millis();
          }
          else
          {
            // We haven't got any data, so let's pause to allow some to
            // arrive
            delay(kNetworkDelay);
          }
        }
      }
      else
      {
        Serial.print("Failed to skip response headers: ");
        Serial.println(err);
      }
    }
    else
    {
      Serial.print("Getting response failed: ");
      Serial.println(err);
    }
  }
  else
  {
    Serial.print("Connect failed: ");
    Serial.println(err);
  }
  http.stop();
}

int find_max(int nums[], int size)
{
  int max_num = nums[0];
  for (int i = 1; i < size; ++i)
  {
    if (nums[i] > max_num)
    {
      max_num = nums[i];
    }
  }
  return max_num;
}

int find_min(int nums[], int size)
{
  int min_num = nums[0];
  for (int i = 1; i < size; ++i)
  {
    if (nums[i] < min_num)
    {
      min_num = nums[i];
    }
  }
  return min_num;
}

double find_median(int nums[], int size)
{
  std::sort(nums, nums + size);
  // If even, return average of the two elements
  if (size % 2 == 0)
  {
    int mid1 = nums[size / 2 - 1];
    int mid2 = nums[size / 2];
    return (mid1 + mid2) / 2.0;
  }
  // If odd, return middle element
  else
  {
    return nums[size / 2];
  }
}

void loop()
{
  // Update sig and prev_sig queue
  sig = analogRead(PULSE_SENSOR_PIN);
  prev_sig[prev_sig_pos] = sig;

  // Conditions for THE ONE 5 BEFORE being a "peak":
  // 1. Is max in prev_sig queue
  // 2. (peak - (5 behind)) is at least 80% of (peak-min)
  // 3. (peak - (5 ahead)) is at least 20% of (peak-min)

  const int point_behind = (prev_sig_pos - 10 + prev_sig_len) % prev_sig_len;
  const int peak = (prev_sig_pos - 5 + prev_sig_len) % prev_sig_len;
  const int point_ahead = prev_sig_pos;
  const int min_num = find_min(prev_sig, prev_sig_len);

  float ratio_behind = (float)(prev_sig[peak] - prev_sig[point_behind]) / (prev_sig[peak] - min_num + 1);
  float ratio_ahead = (float)(prev_sig[peak] - prev_sig[point_ahead]) / (prev_sig[peak] - min_num + 1);

  // if (find_max(prev_sig, prev_sig_len) == prev_sig[peak])
  //   Serial.println("11111111111111");
  // if (ratio_behind >= 0.8)
  //   Serial.println("22222222222222");
  // if (ratio_ahead >= 0.2)
  //   Serial.println("33333333333333");

  if (find_max(prev_sig, prev_sig_len) == prev_sig[peak] && // Condition #1
      ratio_behind >= 0.8 &&                                // Condition #2
      ratio_ahead >= 0.2)                                   // Condition #3
  {
    // Enqueue new element
    int peak_time = millis();
    recent_peaks[recent_peaks_pos] = peak_time;

    // Increment queue_pos and cycle
    ++recent_peaks_pos;
    recent_peaks_pos %= peaks_queue_len;
  }
  // Increment signal queue position
  ++prev_sig_pos;
  prev_sig_pos %= prev_sig_len;

  // Calculate heart rate and POST to EC2 instance
  if (millis() - last_calculation >= POST_INTERVAL)
  {
    // Calculate times between each peak
    int most_recent = (recent_peaks_pos - 1 + peaks_queue_len) % peaks_queue_len;
    int least_recent = recent_peaks_pos;

    int total_time = recent_peaks[most_recent] - recent_peaks[least_recent];
    // Average time between beats in ms
    float average_time = (float)total_time / (peaks_queue_len - 1);
    float average_bpm = 1000 / average_time * 60;

    Serial.println(average_bpm);
    Serial.println();

    WiFiClient c;
    HttpClient http(c);

    int heart_rate = (int)average_bpm;

    String url = "/api/add_data/" + String(heart_rate);

    int err = http.get("35.160.204.3", 3001, url.c_str(), NULL);

    if (err == 0)
    {
      Serial.println("startedRequest ok");
      err = http.responseStatusCode();
      if (err >= 0)
      {
        Serial.print("Got status code: ");
        Serial.println(err);

        // Usually you'd check that the response code is 200 or a
        // similar "success" code (200-299) before carrying on,
        // but we'll print out whatever response we get

        err = http.skipResponseHeaders();
        if (err >= 0)
        {
          int bodyLen = http.contentLength();
          Serial.print("Content length is: ");
          Serial.println(bodyLen);
          Serial.println();
          Serial.println("Body returned follows:");

          // Now we've got to the body, so we can print it out
          unsigned long timeoutStart = millis();
          char c;
          // Whilst we haven't timed out & haven't reached the end of the body
          while ((http.connected() || http.available()) &&
                 ((millis() - timeoutStart) < kNetworkTimeout))
          {
            if (http.available())
            {
              c = http.read();
              // Print out this character
              Serial.print(c);

              bodyLen--;
              // We read something, reset the timeout counter
              timeoutStart = millis();
            }
            else
            {
              // We haven't got any data, so let's pause to allow some to arrive
              delay(kNetworkDelay);
            }
          }
        }
        else
        {
          Serial.print("Failed to skip response headers: ");
          Serial.println(err);
        }
      }
      else
      {
        Serial.print("Getting response failed: ");
        Serial.println(err);
      }
    }
    else
    {
      Serial.print("Connect failed: ");
      Serial.println(err);
    }
    http.stop();

    // And just stop, now that we've tried a download
    Serial.println();
    Serial.println();

    // Update time of last calculation
    last_calculation = millis();
  }

  // Short delay
  delay(interval_delay);
}