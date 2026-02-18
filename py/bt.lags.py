import numpy as np
from scipy.io import wavfile
import random
import pyaudio
import tkinter as tk
from tkinter import ttk
import threading

# Global variables for audio data and positions
left = None
right = None
fs = None
pos_left = 0
pos_right = 0
accumulated_desync = 0  # Tracks channel drift
prev_left = None
prev_right = None
stream = None

# Waveform display buffer (last 500ms)
WAVEFORM_MS = 500
waveform_left = None
waveform_right = None
waveform_pos = 0

# Bluetooth packet parameters
BT_PACKET_SIZE_MS = 2.5  # Typical BT packet size in milliseconds
packet_size_samples = None  # Will be calculated based on sample rate

# Connection quality model state
connection_state = {
    'quality': 1.0,  # 0.0 (terrible) to 1.0 (perfect)
    'target_quality': 1.0,
    'degradation_active': False,
    'degradation_duration': 0,
    'degradation_remaining': 0,
    'packet_loss_prob': 0.0,
    'desync_rate': 0.0,  # packets per chunk to drift
    'codec_degradation': 0.0,  # 0.0 (none) to 1.0 (severe)
    'buffer_issues': 0.0,  # probability of buffer-related glitches
    'drift_direction': 1,  # persistent drift direction during degradation
    'affected_channel': 'both',  # 'left', 'right', or 'both' — which channel degrades
    'left_in_burst': False,  # left channel currently in a loss burst
    'right_in_burst': False  # right channel currently in a loss burst
}

# Tkinter variables
glitch_prob_var = None
degradation_severity_var = None
degradation_duration_var = None
recovery_speed_var = None
desync_severity_var = None
dropout_threshold_var = None
stutter_threshold_var = None
codec_degrade_threshold_var = None
bt_packet_size_var = None
burst_persistence_var = None

# Cached GUI values — written by GUI thread, read by audio thread (plain floats, no Tcl)
gui_params = {
    'glitch_prob': 0.02,
    'severity': 0.7,
    'duration': 2000.0,
    'recovery': 0.05,
    'desync_sev': 1.0,
    'dropout_thresh': 0.4,
    'stutter_thresh': 0.3,
    'codec_thresh': 0.5,
    'burst_persist': 0.95,
    'bt_packet_ms': 2.5,
}

def load_audio(input_file):
    global fs, left, right, packet_size_samples
    try:
        fs, data = wavfile.read(input_file)
        is_int = np.issubdtype(data.dtype, np.integer)
        max_val = np.iinfo(data.dtype).max if is_int else 1.0

        if data.ndim == 1:
            data = np.stack([data, data], axis=1)

        left = data[:, 0].astype(float) / max_val
        right = data[:, 1].astype(float) / max_val

        # Calculate packet size in samples
        packet_size_samples = int((BT_PACKET_SIZE_MS / 1000.0) * fs)
    except:
        pass

def update_connection_quality(degradation_prob, severity, duration_ms, recovery_speed):
    """Update the connection quality state (call once per chunk)"""
    global connection_state

    # Check if we should start a new degradation event
    if not connection_state['degradation_active']:
        if random.random() < degradation_prob:
            # Start degradation
            connection_state['degradation_active'] = True
            connection_state['target_quality'] = 1.0 - severity
            connection_state['drift_direction'] = random.choice([-1, 1])
            # TWS: degradation usually hits one earbud more than the other
            roll = random.random()
            if roll < 0.4:
                connection_state['affected_channel'] = 'left'
            elif roll < 0.8:
                connection_state['affected_channel'] = 'right'
            else:
                connection_state['affected_channel'] = 'both'

            # Duration in chunks (at 44100 Hz, 512 samples/chunk = ~86 chunks/sec)
            chunks_per_ms = (fs / 512.0) / 1000.0
            duration_chunks = int(duration_ms * chunks_per_ms)
            connection_state['degradation_duration'] = duration_chunks
            connection_state['degradation_remaining'] = duration_chunks
    else:
        # Continue degradation event
        connection_state['degradation_remaining'] -= 1

        # Check if degradation event is over
        if connection_state['degradation_remaining'] <= 0:
            connection_state['degradation_active'] = False
            connection_state['target_quality'] = 1.0

    # Smoothly transition quality toward target
    current_quality = connection_state['quality']
    target_quality = connection_state['target_quality']

    if connection_state['degradation_active']:
        # Fast degradation
        transition_speed = 0.1
    else:
        # Slower recovery (user-configurable)
        transition_speed = recovery_speed

    connection_state['quality'] += (target_quality - current_quality) * transition_speed
    connection_state['quality'] = np.clip(connection_state['quality'], 0.0, 1.0)

    # Update derived parameters based on quality
    quality = connection_state['quality']

    # Packet loss increases as quality decreases
    connection_state['packet_loss_prob'] = (1.0 - quality) * 0.7

    # Desync rate (probability of packet-sized drift per chunk)
    connection_state['desync_rate'] = (1.0 - quality) * 0.4

    # Codec degradation
    connection_state['codec_degradation'] = 1.0 - quality

    # Buffer issues
    connection_state['buffer_issues'] = (1.0 - quality) * 0.3

def _apply_loss_to_channel(chunk, prev_chunk, loss_prob, dropout_thresh, stutter_thresh, burst_key, burst_persist):
    """Apply packet loss to a single channel with burst behavior.
    Once a burst starts, it persists across packets and chunks (sustained dropout)."""
    num_packets = -(-len(chunk) // packet_size_samples)  # ceiling division
    in_burst = connection_state[burst_key]

    for packet_idx in range(num_packets):
        if in_burst:
            # High probability of continuing the burst (creates sustained dropouts)
            effective_prob = max(loss_prob, burst_persist)
        else:
            effective_prob = loss_prob

        if random.random() >= effective_prob:
            in_burst = False
            continue

        in_burst = True
        packet_start = packet_idx * packet_size_samples
        packet_end = min(packet_start + packet_size_samples, len(chunk))

        if in_burst and connection_state[burst_key]:
            # Mid-burst: always silence (real BT dropout = silence)
            chunk[packet_start:packet_end] = 0.0
        else:
            # Burst just starting: roll for effect type
            roll = random.random()
            if roll < dropout_thresh:
                chunk[packet_start:packet_end] = 0.0
            elif roll < dropout_thresh + stutter_thresh:
                if prev_chunk is not None:
                    prev_data = prev_chunk[packet_start:packet_end]
                    chunk[packet_start:packet_end] = prev_data[:packet_end - packet_start]
            else:
                chunk[packet_start:packet_end] *= random.uniform(0.1, 0.5)

    connection_state[burst_key] = in_burst
    return chunk


def apply_packet_loss_effects(chunk_left, chunk_right, prev_left, prev_right,
                              dropout_thresh, stutter_thresh, burst_persist):
    """Apply effects of packet loss independently per channel (TWS behavior)"""
    loss_prob = connection_state['packet_loss_prob']

    if loss_prob <= 0:
        # No degradation — end any active bursts
        connection_state['left_in_burst'] = False
        connection_state['right_in_burst'] = False
        return chunk_left, chunk_right

    affected = connection_state['affected_channel']

    # Per-channel loss probabilities: affected channel gets full loss,
    # unaffected channel gets a small fraction (crosstalk/relay degradation)
    if affected == 'left':
        left_prob = loss_prob
        right_prob = loss_prob * 0.1
    elif affected == 'right':
        left_prob = loss_prob * 0.1
        right_prob = loss_prob
    else:
        left_prob = loss_prob
        right_prob = loss_prob

    chunk_left = _apply_loss_to_channel(chunk_left, prev_left, left_prob,
                                        dropout_thresh, stutter_thresh,
                                        'left_in_burst', burst_persist)
    chunk_right = _apply_loss_to_channel(chunk_right, prev_right, right_prob,
                                         dropout_thresh, stutter_thresh,
                                         'right_in_burst', burst_persist)

    return chunk_left, chunk_right

def apply_codec_degradation(chunk, codec_degrade_thresh):
    """Apply codec degradation effects (bitcrush, downsample).
    codec_degrade_thresh: quality threshold below which codec degrades.
    0 = never degrade, 1 = always degrade."""
    quality = connection_state['quality']

    if quality >= codec_degrade_thresh or codec_degrade_thresh <= 0:
        return chunk

    # How far below threshold: 0 = just entered, 1 = worst possible
    effect_strength = (codec_degrade_thresh - quality) / codec_degrade_thresh

    if random.random() < effect_strength:
        effect_type = random.choice(['bitcrush', 'downsample', 'both'])

        if effect_type == 'bitcrush' or effect_type == 'both':
            # Reduce bit depth: 16 bits down to 6 bits
            bits = int(16 - effect_strength * 10)
            bits = max(6, bits)
            levels = 2 ** bits
            chunk = np.round(chunk * levels) / levels

        if effect_type == 'downsample' or effect_type == 'both':
            # Simulate lower sample rate: factor 1-4
            factor = int(1 + effect_strength * 3)
            if factor > 1:
                downsampled = chunk[::factor]
                chunk = np.repeat(downsampled, factor)[:len(chunk)]

    return chunk

def apply_desync_drift(desync_severity):
    """Update accumulated desync based on connection quality (packet-level jumps)"""
    global accumulated_desync

    # Desync happens in discrete packet-sized jumps when packets are lost/delayed
    drift_probability = connection_state['desync_rate'] * desync_severity

    if random.random() < drift_probability:
        # Use persistent direction so desync accumulates consistently,
        # with a small chance of jitter in the opposite direction
        direction = connection_state['drift_direction']
        if random.random() < 0.15:  # 15% chance of opposite-direction jitter
            direction = -direction
        num_packets = random.randint(1, 3)  # Lose 1-3 packets
        drift_samples = direction * num_packets * packet_size_samples

        accumulated_desync += drift_samples

    # Slowly recover toward zero only when quality is very good (in packet-sized steps)
    if connection_state['quality'] > 0.9 and abs(accumulated_desync) > packet_size_samples:
        # Recover by one packet at a time, less aggressively than before
        if random.random() < 0.03:  # 3% chance per chunk (was 10%)
            recovery_direction = -1 if accumulated_desync > 0 else 1
            accumulated_desync += recovery_direction * packet_size_samples

    # Clamp to reasonable limits (max ~100ms desync)
    max_desync = int((100 / 1000.0) * fs)  # 100ms in samples
    accumulated_desync = np.clip(accumulated_desync, -max_desync, max_desync)

def apply_buffer_stutter(chunk_left, chunk_right):
    """Apply buffer-related micro-stutters (packet-level)"""
    if random.random() < connection_state['buffer_issues']:
        # Micro-stutter: repeat a packet-sized section
        num_packets_in_chunk = -(-len(chunk_left) // packet_size_samples)  # ceiling division

        if num_packets_in_chunk > 1:
            # Pick a random packet to repeat
            packet_idx = random.randint(0, num_packets_in_chunk - 2)
            packet_start = packet_idx * packet_size_samples
            packet_end = min(packet_start + packet_size_samples, len(chunk_left))

            repeat_section_left = chunk_left[packet_start:packet_end].copy()
            repeat_section_right = chunk_right[packet_start:packet_end].copy()

            # Insert the repeated section at the next packet position
            insert_pos = packet_end
            insert_end = min(insert_pos + len(repeat_section_left), len(chunk_left))
            chunk_left[insert_pos:insert_end] = repeat_section_left[:insert_end - insert_pos]
            chunk_right[insert_pos:insert_end] = repeat_section_right[:insert_end - insert_pos]

    return chunk_left, chunk_right

def callback(in_data, frame_count, time_info, status_flags):
    global pos_left, pos_right, prev_left, prev_right, accumulated_desync

    # Read cached GUI params (updated by GUI thread, no Tcl calls here)
    p = gui_params
    glitch_prob = p['glitch_prob']
    severity = p['severity']
    duration = p['duration']
    recovery = p['recovery']
    desync_sev = p['desync_sev']
    dropout_thresh = p['dropout_thresh']
    stutter_thresh = p['stutter_thresh']
    codec_thresh = p['codec_thresh']
    burst_persist = p['burst_persist']

    global packet_size_samples, BT_PACKET_SIZE_MS
    BT_PACKET_SIZE_MS = p['bt_packet_ms']
    packet_size_samples = int((BT_PACKET_SIZE_MS / 1000.0) * fs)

    # Update connection quality model
    update_connection_quality(glitch_prob, severity, duration, recovery)

    # Apply desync drift
    apply_desync_drift(desync_sev)

    # Calculate actual positions with desync
    pos_right = int((pos_left + accumulated_desync) % len(left))

    # Get next chunk for left with wrap-around
    end_left = pos_left + frame_count
    if end_left > len(left):
        chunk_left = np.concatenate((left[pos_left:], left[0:end_left - len(left)]))
    else:
        chunk_left = left[pos_left:end_left].copy()

    # Get next chunk for right with wrap-around
    end_right = pos_right + frame_count
    if end_right > len(right):
        chunk_right = np.concatenate((right[pos_right:], right[0:end_right - len(right)]))
    else:
        chunk_right = right[pos_right:end_right].copy()

    # Apply combined effects based on connection quality

    # 1. Packet loss effects (dropout, stutter)
    chunk_left, chunk_right = apply_packet_loss_effects(
        chunk_left, chunk_right, prev_left, prev_right,
        dropout_thresh, stutter_thresh, burst_persist
    )

    # 2. Codec degradation
    chunk_left = apply_codec_degradation(chunk_left, codec_thresh)
    chunk_right = apply_codec_degradation(chunk_right, codec_thresh)

    # 3. Buffer micro-stutters
    chunk_left, chunk_right = apply_buffer_stutter(chunk_left, chunk_right)

    # Save for next iteration
    prev_left = chunk_left.copy()
    prev_right = chunk_right.copy()

    # Write to waveform ring buffer
    global waveform_pos
    if waveform_left is not None:
        buf_len = len(waveform_left)
        n = len(chunk_left)
        end = waveform_pos + n
        if end <= buf_len:
            waveform_left[waveform_pos:end] = chunk_left
            waveform_right[waveform_pos:end] = chunk_right
        else:
            first = buf_len - waveform_pos
            waveform_left[waveform_pos:] = chunk_left[:first]
            waveform_right[waveform_pos:] = chunk_right[:first]
            waveform_left[:end - buf_len] = chunk_left[first:]
            waveform_right[:end - buf_len] = chunk_right[first:]
        waveform_pos = end % buf_len

    # Advance position
    pos_left = (pos_left + frame_count) % len(left)

    # Combine into interleaved stereo, clamp to [-1, 1]
    out_data = np.clip(
        np.column_stack((chunk_left, chunk_right)).ravel(), -1.0, 1.0
    ).astype(np.float32)
    return (out_data.tobytes(), pyaudio.paContinue)

def start_stream():
    global stream, pos_left, pos_right, prev_left, prev_right, accumulated_desync
    global connection_state, waveform_left, waveform_right, waveform_pos

    pos_left = 0
    pos_right = 0
    prev_left = None
    prev_right = None
    accumulated_desync = 0

    # Init waveform buffer
    buf_samples = int((WAVEFORM_MS / 1000.0) * fs)
    waveform_left = np.zeros(buf_samples)
    waveform_right = np.zeros(buf_samples)
    waveform_pos = 0

    # Reset connection state
    connection_state['quality'] = 1.0
    connection_state['target_quality'] = 1.0
    connection_state['degradation_active'] = False
    connection_state['left_in_burst'] = False
    connection_state['right_in_burst'] = False

    p = pyaudio.PyAudio()
    stream = p.open(format=pyaudio.paFloat32,
                    channels=2,
                    rate=fs,
                    output=True,
                    frames_per_buffer=512,
                    stream_callback=callback)

    threading.Thread(target=stream.start_stream, daemon=True).start()

def stop_stream():
    global stream
    if stream:
        stream.stop_stream()
        stream.close()
        stream = None

def test_stereo(channel):
    """Play a 0.5s 440Hz tone in only one ear to verify stereo separation"""
    stop_stream()
    sample_rate = fs or 44100
    duration = 0.5
    t = np.linspace(0, duration, int(sample_rate * duration), endpoint=False)
    tone = (np.sin(2 * np.pi * 440 * t) * 0.5).astype(np.float32)
    silence = np.zeros_like(tone)

    if channel == 'left':
        stereo = np.column_stack((tone, silence)).ravel()
    else:
        stereo = np.column_stack((silence, tone)).ravel()

    p = pyaudio.PyAudio()
    s = p.open(format=pyaudio.paFloat32, channels=2, rate=sample_rate, output=True)
    s.write(stereo.tobytes())
    s.stop_stream()
    s.close()
    p.terminate()

# GUI setup
root = tk.Tk()
root.title("Physical Bluetooth Connection Simulator")
root.geometry("500x800")  # Set reasonable window size

main_frame = ttk.Frame(root, padding="5")
main_frame.pack(fill=tk.BOTH, expand=True)

# Create notebook (tabbed interface)
notebook = ttk.Notebook(main_frame)
notebook.pack(fill=tk.BOTH, expand=True, pady=5)

# Tab 1: Connection Quality
quality_tab = ttk.Frame(notebook, padding="10")
notebook.add(quality_tab, text="Connection Quality")

tk.Label(quality_tab, text="BT Packet Size (ms)", font=('TkDefaultFont', 9, 'bold')).pack()
tk.Label(quality_tab, text="SBC: ~2.9ms | AAC: ~2-4ms | aptX: ~1-2ms", font=('TkDefaultFont', 7)).pack()
bt_packet_size_var = tk.DoubleVar(value=2.5)
tk.Scale(quality_tab, from_=1.0, to=10.0, variable=bt_packet_size_var,
         orient="horizontal", length=300, resolution=0.1).pack(pady=2)

tk.Label(quality_tab, text="Event Probability", font=('TkDefaultFont', 9, 'bold')).pack(pady=(10, 0))
tk.Label(quality_tab, text="How often connection degrades", font=('TkDefaultFont', 7)).pack()
glitch_prob_var = tk.DoubleVar(value=0.02)
tk.Scale(quality_tab, from_=0.0, to=0.2, variable=glitch_prob_var,
         orient="horizontal", length=300, resolution=0.001).pack(pady=2)

tk.Label(quality_tab, text="Degradation Severity", font=('TkDefaultFont', 9, 'bold')).pack(pady=(10, 0))
tk.Label(quality_tab, text="How bad the connection gets", font=('TkDefaultFont', 7)).pack()
degradation_severity_var = tk.DoubleVar(value=0.7)
tk.Scale(quality_tab, from_=0.0, to=1.0, variable=degradation_severity_var,
         orient="horizontal", length=300, resolution=0.01).pack(pady=2)

tk.Label(quality_tab, text="Event Duration (ms)", font=('TkDefaultFont', 9, 'bold')).pack(pady=(10, 0))
tk.Label(quality_tab, text="How long degradation lasts", font=('TkDefaultFont', 7)).pack()
degradation_duration_var = tk.DoubleVar(value=2000)
tk.Scale(quality_tab, from_=500, to=8000, variable=degradation_duration_var,
         orient="horizontal", length=300, resolution=100).pack(pady=2)

tk.Label(quality_tab, text="Recovery Speed", font=('TkDefaultFont', 9, 'bold')).pack(pady=(10, 0))
tk.Label(quality_tab, text="How quickly connection recovers", font=('TkDefaultFont', 7)).pack()
recovery_speed_var = tk.DoubleVar(value=0.05)
tk.Scale(quality_tab, from_=0.01, to=0.5, variable=recovery_speed_var,
         orient="horizontal", length=300, resolution=0.01).pack(pady=2)

# Tab 2: Effect Parameters
effects_tab = ttk.Frame(notebook, padding="10")
notebook.add(effects_tab, text="Effect Parameters")

tk.Label(effects_tab, text="Desync Severity", font=('TkDefaultFont', 9, 'bold')).pack(pady=(5, 0))
tk.Label(effects_tab, text="How much channels drift apart", font=('TkDefaultFont', 7)).pack()
desync_severity_var = tk.DoubleVar(value=1.0)
tk.Scale(effects_tab, from_=0.0, to=2.0, variable=desync_severity_var,
         orient="horizontal", length=300, resolution=0.01).pack(pady=2)

tk.Label(effects_tab, text="Dropout Threshold", font=('TkDefaultFont', 9, 'bold')).pack(pady=(10, 0))
tk.Label(effects_tab, text="Probability of silence during packet loss", font=('TkDefaultFont', 7)).pack()
dropout_threshold_var = tk.DoubleVar(value=0.4)
tk.Scale(effects_tab, from_=0.0, to=1.0, variable=dropout_threshold_var,
         orient="horizontal", length=300, resolution=0.01).pack(pady=2)

tk.Label(effects_tab, text="Stutter Threshold", font=('TkDefaultFont', 9, 'bold')).pack(pady=(10, 0))
tk.Label(effects_tab, text="Probability of audio repeating", font=('TkDefaultFont', 7)).pack()
stutter_threshold_var = tk.DoubleVar(value=0.3)
tk.Scale(effects_tab, from_=0.0, to=1.0, variable=stutter_threshold_var,
         orient="horizontal", length=300, resolution=0.01).pack(pady=2)

tk.Label(effects_tab, text="Burst Persistence", font=('TkDefaultFont', 9, 'bold')).pack(pady=(10, 0))
tk.Label(effects_tab, text="How long dropouts sustain (higher = longer gaps)", font=('TkDefaultFont', 7)).pack()
burst_persistence_var = tk.DoubleVar(value=0.95)
tk.Scale(effects_tab, from_=0.5, to=0.99, variable=burst_persistence_var,
         orient="horizontal", length=300, resolution=0.01).pack(pady=2)

tk.Label(effects_tab, text="Codec Degradation Threshold", font=('TkDefaultFont', 9, 'bold')).pack(pady=(10, 0))
tk.Label(effects_tab, text="Quality below which codec degrades", font=('TkDefaultFont', 7)).pack()
codec_degrade_threshold_var = tk.DoubleVar(value=0.5)
tk.Scale(effects_tab, from_=0.0, to=1.0, variable=codec_degrade_threshold_var,
         orient="horizontal", length=300, resolution=0.01).pack(pady=2)

# Tab 3: Waveform
waveform_tab = ttk.Frame(notebook, padding="5")
notebook.add(waveform_tab, text="Waveform")

WAVE_W, WAVE_H = 480, 100  # per-channel canvas size
wave_canvas_left = tk.Canvas(waveform_tab, width=WAVE_W, height=WAVE_H,
                             bg='black', highlightthickness=0)
tk.Label(waveform_tab, text="Left", font=('TkDefaultFont', 8)).pack()
wave_canvas_left.pack(fill=tk.X, padx=5, pady=2)
wave_canvas_right = tk.Canvas(waveform_tab, width=WAVE_W, height=WAVE_H,
                              bg='black', highlightthickness=0)
tk.Label(waveform_tab, text="Right", font=('TkDefaultFont', 8)).pack()
wave_canvas_right.pack(fill=tk.X, padx=5, pady=2)

# Pre-create polyline items (updated in-place via coords)
wave_line_left = wave_canvas_left.create_line(0, 0, 0, 0, fill='#4090ff', width=1)
wave_line_right = wave_canvas_right.create_line(0, 0, 0, 0, fill='#ff4090', width=1)
# Zero-line references
wave_canvas_left.create_line(0, WAVE_H // 2, WAVE_W, WAVE_H // 2, fill='#333333', width=1)
wave_canvas_right.create_line(0, WAVE_H // 2, WAVE_W, WAVE_H // 2, fill='#333333', width=1)

# Status display (outside tabs, always visible)
ttk.Separator(main_frame, orient='horizontal').pack(fill='x', pady=5)

status_frame = ttk.LabelFrame(main_frame, text="Status", padding="5")
status_frame.pack(fill='x', pady=5)

quality_label = tk.Label(status_frame, text="Connection Quality: 100%",
                         font=('TkDefaultFont', 9))
quality_label.pack(pady=2)

desync_label = tk.Label(status_frame, text="Desync: 0 packets (0.0 ms)",
                        font=('TkDefaultFont', 9))
desync_label.pack(pady=2)

def update_status():
    """Update status display and sync GUI slider values to cache"""
    # Sync slider values to cache (GUI thread -> plain dict, no Tcl in audio thread)
    gui_params['glitch_prob'] = glitch_prob_var.get()
    gui_params['severity'] = degradation_severity_var.get()
    gui_params['duration'] = degradation_duration_var.get()
    gui_params['recovery'] = recovery_speed_var.get()
    gui_params['desync_sev'] = desync_severity_var.get()
    gui_params['dropout_thresh'] = dropout_threshold_var.get()
    gui_params['stutter_thresh'] = stutter_threshold_var.get()
    gui_params['codec_thresh'] = codec_degrade_threshold_var.get()
    gui_params['burst_persist'] = burst_persistence_var.get()
    gui_params['bt_packet_ms'] = bt_packet_size_var.get()

    if stream and stream.is_active():
        quality_pct = connection_state['quality'] * 100
        affected = connection_state['affected_channel'].upper() if quality_pct < 80 else ''
        status_tag = f'[{affected}]' if affected else '[OK]'
        quality_label.config(
            text=f"Quality: {quality_pct:.0f}% {status_tag}",
            fg='red' if quality_pct < 50 else 'orange' if quality_pct < 80 else 'green'
        )

        desync_ms = (accumulated_desync / fs) * 1000 if fs else 0
        desync_packets = accumulated_desync / packet_size_samples if packet_size_samples else 0
        desync_label.config(
            text=f"Desync: {desync_packets:+.1f} pkts ({desync_ms:+.1f} ms)"
        )

        # Update waveform if tab is visible
        if waveform_left is not None and notebook.index(notebook.select()) == 2:
            buf_len = len(waveform_left)
            w = wave_canvas_left.winfo_width()
            h = WAVE_H
            mid = h / 2.0
            # Downsample to canvas pixel width
            num_pts = max(2, w)
            step = max(1, buf_len // num_pts)
            idx = np.arange(0, buf_len, step)
            ordered_idx = (idx + waveform_pos) % buf_len
            wl = waveform_left[ordered_idx]
            wr = waveform_right[ordered_idx]
            n = len(wl)
            xs = np.linspace(0, w, n)
            # Convert amplitude [-1,1] to canvas y [0, h]
            yl = (mid - wl * mid).clip(0, h)
            yr = (mid - wr * mid).clip(0, h)
            # Build flat coordinate lists for coords()
            coords_l = np.empty(n * 2)
            coords_l[0::2] = xs
            coords_l[1::2] = yl
            coords_r = np.empty(n * 2)
            coords_r[0::2] = xs
            coords_r[1::2] = yr
            wave_canvas_left.coords(wave_line_left, *coords_l.tolist())
            wave_canvas_right.coords(wave_line_right, *coords_r.tolist())

    root.after(100, update_status)

# Buttons
button_frame = ttk.Frame(main_frame)
button_frame.pack(pady=5)
ttk.Button(button_frame, text="▶ Play", command=lambda: [start_stream(), update_status()]).pack(side='left', padx=3)
ttk.Button(button_frame, text="⏹ Stop", command=stop_stream).pack(side='left', padx=3)
ttk.Button(button_frame, text="🔊 L Test", command=lambda: test_stereo('left')).pack(side='left', padx=3)
ttk.Button(button_frame, text="🔊 R Test", command=lambda: test_stereo('right')).pack(side='left', padx=3)

# Load audio file
load_audio('D:\\Big projects\\Memory Well\\SharedBlue.wav')

# If no input file, generate a test tone
if left is None:
    fs = 44100
    t = np.linspace(0, 5, 5 * fs, endpoint=False)
    left = np.sin(2 * np.pi * 440 * t) * 0.9
    right = np.sin(2 * np.pi * 880 * t) * 0.9
    wavfile.write('test_input.wav', fs, (np.stack([left, right], axis=1) * 32767).astype(np.int16))
    load_audio('test_input.wav')

root.mainloop()

# claude --resume 205be4f8-2266-47e7-bdd4-b7c5f59dcc19