on_power_on():
  init_serial(baud=115200)

  init_FSR_sensors():
    for each FSR in [GPIO1..GPIO8]:
      set_pin_mode(pin, INPUT)
      verify_pin_reads_nonzero()
      if fail → log_error("FSR {pin} not detected")

  init_IMU():
    begin_I2C(SDA, SCL)
    if MPU6050.begin() fails:
      log_error("IMU not found, check wiring")
      halt()
    set_accel_range(±4g)
    # ±4g chosen because running peaks at 2-3g,
    # gives headroom without losing resolution
    set_gyro_range(±500°/s)
    # foot rotates 200-400°/s during running,
    # ±500 covers this with headroom

  init_flash_storage():
    mount_SPIFFS()
    if mount fails:
      format_SPIFFS()             # first boot
      remount_SPIFFS()

  check_battery():
    voltage = read_battery_pin()
    if voltage < LOW_THRESH:
      flash_LED(red)
      halt()

  check_existing_data():
    files = list_files("/runs/")
    if files.count > 0:
      notify_frontend("previous_data_exists")

  start_wifi_ap():
    WiFi.softAP("SoleSense", password="solesense123")
    ip = WiFi.softAPIP()          # 192.168.4.1
    log("AP started at " + ip)

  start_web_server():
    server.on("/", serve_index)
    server.on("/data", handle_data_request)
    server.on("/start", handle_start_run)
    server.on("/stop", handle_stop_run)
    server.on("/status", handle_status)
    server.begin()

  serve_frontend():
    load_index_html from SPIFFS
    send to client browser

  show_device_status():           # "Ready" / "Recording" / "Error"
  show_battery_level():           # % from voltage reading
  show_sensor_status():           # green/red per FSR + IMU
  show_stored_data():             # list of past run files + sizes

  start_run()     → POST /start  → triggers pre-run sequence
  view_last_run() → GET  /data   → loads most recent CSV
  delete_data()   → DELETE /data → wipes /runs/ directory
  export_report() → GET  /export → sends CSV file download

on_start_click():
  send_start_cmd(device):
    HTTP POST to ESP32 /start endpoint
    wait for 200 OK response
    if timeout → show_error("Device not responding")

  check_sensors():
    for each FSR:
      raw = analogRead(pin)
      if raw == 0 OR raw == 4095:
        flag_sensor_error(pin)
        # 0 = disconnected, 4095 = shorted
    if IMU.testConnection() == false:
      flag_IMU_error()
    if any errors → abort_run(), notify_user()

  calibrate_sensors():
    notify_user("Stand still for 3 seconds...")
    for t in 0..300:              # 3 sec at 100Hz
      baseline[t] = read_all_sensors()
      sleep(10ms)
    baseline = average(baseline[])
    store_baseline(baseline)
    # 3 seconds chosen: long enough to average noise,
    # short enough to not be annoying to user

  create_csv(run_id):
    filename = "/runs/run_" + timestamp + ".csv"
    open file for write

  write_csv_header():
    "time_ms, FSR1, FSR2, FSR3, FSR4,
     FSR5, FSR6, FSR7, FSR8,
     accel_x, accel_y, accel_z,
     gyro_x, gyro_y, gyro_z"

while run_active == True:
  timestamp = millis()

  read_FSR_sensors():
    for each FSR pin:
      raw[i]        = analogRead(pin[i])
      voltage[i]    = raw[i] * (3.3 / 4095.0)
      resistance[i] = (3.3 / voltage[i] - 1.0) * 10000
      force[i]      = map_resistance_to_force(resistance[i])

  read_accel_xyz():
    [ax, ay, az] = IMU.getAcceleration()   # in g's

  read_gyro_xyz():
    [gx, gy, gz] = IMU.getRotation()       # in °/s

  data = subtract_baseline(data):
    for each sensor:
      calibrated[i] = raw[i] - baseline[i]

  row = format_csv_row(data):
    row = timestamp + "," +
          FSR1..8   + "," +
          ax,ay,az  + "," +
          gx,gy,gz

  write_to_flash(run_id, row):
    append row to open CSV file
    if write fails → log_error, set run_active = False

  sleep(10ms)                     # maintains 100 Hz
  # 100 Hz chosen: captures 20-30 points per stride,
  # enough to detect heel strike peaks and toe-off shapes

  on_stop_click()           # user presses Stop on website
  on_hw_button_press()      # physical button on board
  storage_limit_reached()   # SPIFFS < 10KB remaining
  time_limit_reached()      # 60 min max

  → run_active = False
  → flush_buffer()
  → close_csv_file()
  → notify_frontend("run_complete")
  → show_download_link(run_id)

data = fetch_csv(run_id):
  open "/runs/run_{id}.csv" from SPIFFS
  read all bytes into buffer

data = parse_csv(data):
  split by newline → rows[]
  split each row by comma → fields[]
  cast each field to float

remove_incomplete_rows(data):
  if row has fewer than 15 columns → discard row

remove_outliers(data):
  for each sensor column:
    mean = average(column)
    std  = stdev(column)
    if value > mean + 3*std → replace with mean
    # 3-sigma: only 0.3% chance of being real data
    # anything beyond this is electrical noise

smooth_noise(data):
  apply moving average (window = 5 samples) per column
  # reduces noise without losing step shape

detect_heel_strikes(data):
  look for spike in FSR7 AND FSR8 simultaneously
  threshold: force > 1.5x running average of heel FSRs
  record timestamp → heel_strike_times[]
  # 1.5x is relative so it self-adjusts per runner weight

detect_toe_offs(data):
  look for drop in FSR1 AND FSR2
  force drops below 10% of their peak → toe_off_times[]

steps = split_into_steps(data):
  pair each heel_strike with next toe_off
  steps[] = [{start, end, duration, FSR[], IMU[]}]

calc_cadence(steps):
  cadence = (steps.count / total_time_sec) * 60
  # steps per minute

calc_contact_time(steps):
  for each step:
    contact_time = toe_off_time - heel_strike_time  # ms

calc_peak_pressure(steps):
  for each step:
    peak = max(all FSR values during step)

HEEL STRIKE ANALYSIS:
calc_heel_strike(steps):
  for each step:
    heel_force  = mean(FSR7, FSR8)
    toe_force   = mean(FSR1, FSR2)
    ball_force  = mean(FSR3, FSR4)
    total_force = heel_force + ball_force + toe_force

    heel_ratio  = heel_force / total_force * 100   # as %

    if heel_ratio > 60%:
      flag("Heel Striker", severity=HIGH)
      recommend("Land with foot closer to body,
                 shorten stride, aim for midfoot contact")

  # Why 60%?
  # Neutral runners distribute ~40-50% load to heel
  # Above 60% = dominant heel striker
  # Source: Lieberman et al. 2010, Nature


TOE STRIKE / BALL-OF-FOOT ANALYSIS:
calc_toe_strike(steps):
  for each step:
    toe_force     = mean(FSR1, FSR2)
    ball_force    = mean(FSR3, FSR4)
    heel_force    = mean(FSR7, FSR8)
    total_force   = heel_force + ball_force + toe_force

    forefoot_ratio = (toe_force + ball_force) / total_force * 100

    if forefoot_ratio > 65%:
      flag("Toe/Forefoot Striker", severity=MEDIUM)
      recommend("Forefoot striking increases calf and
                 Achilles load. Allow heel to make light
                 contact to distribute impact.")

    ball_ratio = ball_force / total_force * 100
    if ball_ratio > 50% AND toe_force < 10%:
      flag("Running on Balls of Feet", severity=MEDIUM)
      recommend("Relax ankles slightly, allow natural
                 foot splay on landing")

  # Why 65% forefoot?
  # Neutral: heel ~40-50%, midfoot ~30-40%, toe ~10-20%
  # Above 65% forefoot = clearly forefoot dominant
  # Risk: Achilles tendinopathy, calf strain


LEFT vs RIGHT HEEL — PRONATION:
calc_heel_pronation(steps):
  for each step:
    inside_heel  = FSR7    # medial / arch side
    outside_heel = FSR8    # lateral / pinky side

    difference = outside_heel - inside_heel
    # positive = more load on outside → supination
    # negative = more load on inside  → overpronation

    NEUTRAL_RANGE = (-15, +15)
    # ±15 raw units ≈ 5-8% force difference
    # within this = normal healthy distribution
    # calibrate after real user trials

    if difference > +15:
      severity = MEDIUM if difference < +30 else HIGH
      flag("Supination (Underpronation)",
           severity=severity,
           data="Outside heel: {FSR8}
                 Inside heel:  {FSR7}
                 Difference:   {difference}
                 Neutral range: -15 to +15")
      recommend("Outside heel absorbing more force.
                 Try flexible footwear, calf stretching,
                 lateral hip strengthening.")

    elif difference < -15:
      severity = MEDIUM if difference > -30 else HIGH
      flag("Overpronation",
           severity=severity,
           data="Outside heel: {FSR8}
                 Inside heel:  {FSR7}
                 Difference:   {difference}
                 Neutral range: -15 to +15")
      recommend("Inside heel absorbing more force.
                 Consider motion control shoes, arch support,
                 hip abductor strengthening.")

    else:
      log("Neutral pronation ✓  Difference: {difference}")

  # Formula summary:
  # [outside_data - inside_data] = difference
  # Neutral:      -15 to +15
  # Supination:   > +15  (MEDIUM), > +30 (HIGH)
  # Overpronation:< -15  (MEDIUM), < -30 (HIGH)



LEFT vs RIGHT FOREFOOT SENSORS:
calc_left_right_forefoot(steps):
  left_top  = mean(FSR1, FSR3)    # left toe + left ball
  right_top = mean(FSR2, FSR4)    # right toe + right ball

  difference = left_top - right_top
  # positive = more load on left side
  # negative = more load on right side

  NEUTRAL_RANGE = (-20, +20)
  # ±20 slightly wider than heel range
  # forefoot has more natural step-to-step variation
  # ±20 ≈ 8-10% side difference = still acceptable

  if abs(difference) > 20:
    dominant_side = "left" if difference > 0 else "right"
    flag("Forefoot Side Imbalance",
         severity=MEDIUM,
         data="Left forefoot:  {left_top}
               Right forefoot: {right_top}
               Difference:     {difference}
               Neutral range:  -20 to +20")
    recommend("{dominant_side} forefoot taking more load.
               May indicate compensating for weakness
               on opposite side. Try single-leg
               strength exercises.")


REMAINING BIOMECHANICS CALCULATIONS
calc_loading_rate(steps):
  loading_rate = peak_force / time_to_peak   # N/s
  # fast loading = higher injury risk

calc_pronation(steps):
  # IMU used as secondary confirmation of FSR pronation
  pronation_angle = integrate(gyro_x, contact_time)
  # cross-check against FSR7 vs FSR8 difference

compare_left_right(steps):
  # full foot left vs right using all sensors
  left_total  = mean(FSR1, FSR3, FSR5, FSR7)
  right_total = mean(FSR2, FSR4, FSR6, FSR8)
  asymmetry   = abs(left_total - right_total)
              / mean(all) * 100              # as %

  # HEEL FLAGS
  if heel_ratio > 60%:
    flag("Heel Striker", HIGH)

  if loading_rate > 80 N/s:
    flag("High Impact Loading", HIGH)
    # Milner et al. 2006, Journal of Biomechanics

  # TOE / FOREFOOT FLAGS
  if forefoot_ratio > 65%:
    flag("Toe/Forefoot Striker", MEDIUM)

  if ball_ratio > 50% AND toe_force < 10%:
    flag("Running on Balls of Feet", MEDIUM)

  # PRONATION FLAGS (FSR-based)
  if difference > +15:
    flag("Supination", MEDIUM)
  if difference > +30:
    flag("Supination", HIGH)
  if difference < -15:
    flag("Overpronation", MEDIUM)
  if difference < -30:
    flag("Overpronation", HIGH)

  # FOREFOOT SIDE FLAGS
  if abs(left_right_forefoot_diff) > 20:
    flag("Forefoot Side Imbalance", MEDIUM)

  # CADENCE FLAGS
  if cadence < 160 spm:
    flag("Low Cadence", MEDIUM)
    # Below 160 = overstriding risk
    # Optimal range: 170-180 spm

  # CONTACT TIME FLAGS
  if contact_time > 300ms:
    flag("Slow Ground Contact", LOW)
    # Elite: 160-200ms
    # Recreational normal: 250-300ms
    # Above 300ms = too long on ground

  # FULL BODY ASYMMETRY FLAG
  if asymmetry > 15%:
    flag("Left-Right Imbalance", MEDIUM)
    # Clinical concern threshold: 10-15%

  sort flags by severity (HIGH → MEDIUM → LOW)

  render_summary(metrics):
    total steps, distance estimate,
    avg cadence, run duration

  render_impact_metrics(metrics):
    avg loading rate, avg contact time,
    peak pressure per zone

  render_pressure_map(metrics):
    heatmap of 8 FSR zones:
      blue   = low force
      green  = moderate
      yellow = elevated
      red    = high / flagged
    overlay per zone:
      heel:     "{heel_ratio}%  |  diff: {difference}"
      forefoot: "{forefoot_ratio}%"
      sides:    "L: {left_top}  |  R: {right_top}"

  render_balance_chart(metrics):
    bar chart:
      heel% vs midfoot% vs ball% vs toe%
      left total vs right total

  render_flags(flags):
    for each flag → show:
      - Flag name + severity badge (HIGH/MEDIUM/LOW)
      - Sensor values that triggered it
      - Actual difference number
      - Neutral range for reference
      - Evidence-based recommendation

  show_recommendations(flags):
    for each flag → evidence-based fix with source

  export_report(metrics, flags):
    generate PDF using jsPDF
    filename = "SoleSense_Report_{date}.pdf"
    trigger browser download

accelerometer range: (+-4g) its because running creates ground reaction that is like 2-3x ur body weight per step taken so it to efficiently measure it captures it at +- 4g which is ceiling just above expected peak so we dont clip the sensors by maxing it out or under measuring
Gyroscope range: (±500°/s) from biomechanics research measures goot angular velocity to be roughly around 200-400°/s while running.decided on 500°/s to risk clipping the measurements on fast foot turnover but also not wast angular resolution and to maximize efficiency
3 sec calibration: 3 secs @ 100 hz gives 300 samples to average which is long enough to smooth out any electrical noise and minor body sway but also short enough that it does not annoy users before run
1.5x running average (heel strike detection): when heel hits ground it creates sharp spike relative to the baseline of just walking. Usiing 1.5x means walking/stepping around wont falsely trigger the detection. 
60% heel load threshold (heel striking flag): neutral midfoot runners distribute roughly 40-50% of total foot load through heel zone per step, which means above 60% means runner is heel dominant (Liberman et al. 2010 published in Nature)
65% forefoot ratio (forefoot strike detection): neautral runner puts roughly 10-20% through to zone and 20-30% through ball zone → totals to around 30-50% forefoot load, so above 65% forefoot load means the runner is landing on the front of the foot first and consistently loading it more than normal
Neutral range (±15) FSR units for heel pronation: approx 5-8% difference in force between inside heel sensors (FSR7) and outside heel sensor (FSR8), and ±15 window accounts for natural variation in neutral gait
15% left-right asymmetry threshold: sports science uses 10-15% side-to-side difference as the threshold of clinical concern for sunning asymmetry. Below 10% is normal, but between 10-15% is borderline, and above 15% suggests an imbalance that could result in injury on the weaker side
