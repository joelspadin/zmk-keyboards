# TODO: why doesn't cmake like this specifically for the fuel_gauge library?
# It works fine for drivers/charger.
# zephyr_library_amend()

add_subdirectory_ifdef(CONFIG_FUEL_GAUGE_NPM1300 npm1300)
