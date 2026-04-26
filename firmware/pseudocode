Start

on_power_on():
    initialize_device()
    start_wifi_ap()
    start_web_server()
    serve_frontend()

-------------------------------------

initialize_device():
    init_FSR_sensors()
    init_IMU()
    init_flash_storage()
    check_battery()
    check_existing_data()

-------------------------------------

start_run():
    calibrate_sensors()
    create_new_run_file(run_id)
    record_run_data(run_id)

-------------------------------------

calibrate_sensors():
    for t in calibration_time:
        baseline = read_all_sensors()
        store_baseline(baseline)

-------------------------------------

record_run_data(run_id):
    while run_active:
        data = read_all_sensors()
        data = subtract_baseline(data)
        timestamp = get_timestamp()
        write_csv_row(run_id, data, timestamp)
        sleep(sample_interval)   # ~20ms (50Hz)

-------------------------------------

stop_run():
    run_active = false
    close_csv_file()
    notify_frontend_run_complete()

-------------------------------------

on_website_load():
    show_device_status()
    show_buttons()
    if run_data_exists:
        enable_view_run()

-------------------------------------

process_run_data():
    data = fetch_csv()
    data = clean_data(data)
    steps = detect_steps(data)
    metrics = analyze_steps(steps)
    flags = detect_injury_risks(metrics)
    display_report(metrics, flags)

-------------------------------------

clean_data(data):
    remove_incomplete_rows(data)
    remove_outliers(data)
    smooth_noise(data)
    return data

-------------------------------------

detect_steps(data):
    steps = []
    for i in data:
        if heel_strike_detected(i):
            step = extract_step_window(data, i)
            steps.append(step)
    return steps

-------------------------------------

analyze_steps(steps):
    results = []
    for step in steps:
        metrics = {}
        metrics.duration = calc_step_duration(step)
        metrics.peak_pressure = calc_peak_pressure(step)
        metrics.loading_rate = calc_loading_rate(step)
        metrics.balance = calc_heel_to_toe(step)
        metrics.pronation = calc_pronation(step)
        results.append(metrics)
    return aggregate(results)

-------------------------------------

detect_injury_risks(metrics):
    flags = []

    if metrics.loading_rate > LOADING_RATE_THRESHOLD:
        flags.append("High loading rate")

    if metrics.cadence < CADENCE_THRESHOLD:
        flags.append("Low cadence")

    if metrics.pronation > PRONATION_HIGH_THRESHOLD:
        flags.append("Overpronation")

    if metrics.pronation < PRONATION_LOW_THRESHOLD:
        flags.append("Supination")

    if metrics.heel_to_toe_ratio indicates heel_dominance:
        flags.append("Heel striking")

    if metrics.contact_time > CONTACT_TIME_THRESHOLD:
        flags.append("Long ground contact time")

    if metrics.left_right_difference > ASYMMETRY_THRESHOLD:
        flags.append("Bilateral asymmetry")

    return rank_flags(flags)

-------------------------------------

display_report(metrics, flags):
    render_summary(metrics)
    render_graphs(metrics)
    render_flags(flags)
    show_recommendations(flags)

-------------------------------------

End
