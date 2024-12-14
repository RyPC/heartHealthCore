import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from scipy.signal import find_peaks

# Load the data
file_path = "data.csv"
data = pd.read_csv(file_path)

# Ensure the 'Value' column exists
if 'Value' not in data.columns:
    raise ValueError("CSV must contain a 'Value' column.")

signal = data['Value']

# Detect peaks
peaks, _ = find_peaks(signal, height=0.5, distance=20)  # Adjust height & distance as needed

# Plot the signal with detected peaks
plt.figure(figsize=(10, 6))
plt.plot(signal, marker='o', markersize=3, label='Normalized Signal')
plt.plot(peaks, signal[peaks], "x", label="Detected Peaks")  # Mark peaks
plt.xlabel("Index")
plt.ylabel("Normalized Value")
plt.title("Heart Signal with Detected Peaks")
plt.legend()
plt.grid(True)
plt.show()

# Calculate heart rate
peak_intervals = np.diff(peaks)  # Interval between consecutive peaks
average_interval = np.mean(peak_intervals)  # Average interval in indices
sampling_rate = 1000  # Example: Adjust based on your actual sampling rate in Hz
heart_rate = (sampling_rate / average_interval) * 60  # Beats per minute (BPM)

print(f"Estimated Heart Rate: {heart_rate:.2f} BPM")
