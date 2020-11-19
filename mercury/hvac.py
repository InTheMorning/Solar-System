import json
import logging
import paho.mqtt.client as mqtt
from logging import (debug, info, error, warning)
from serialcom import (setup_serial, read_serial, write_serial)


class HvacState:
    def __init__(self):
        self.serial = None
        self.status = 'Offline'
        self.hvac_code = 0
        self._aux = 0
        self._mode = 0

    @property
    def aux(self):
        if self.hvac_code == 3:
            return 1
        elif self.hvac_code == 2:
            return 0
        else:
            return self._aux

    @aux.setter
    def aux(self, value):
        if value == 1:
            if self.hvac_code == 2:
                self.hvac_code = 3
        elif value == 0:
            if self.hvac_code == 3:
                self.hvac_code = 2
        else:
            raise ValueError("Incorrect aux value")
            value = 0
        self._aux = value

    @property
    def mode(self):
        if self.hvac_code == 3:
            self.aux = 1
            return 2
        else:
            return self._mode

    @mode.setter
    def mode(self, value):
        if value == 2:
            if self.aux:
                self.hvac_code = 3
            else:
                self.hvac_code = 2
        elif value in range(0, 2):
            self.hvac_code = value
        else:
            raise ValueError("Incorrect mode value")
            value = 0
        self._mode = value

    def change_hvac_state(self, hvac_code):
        ser = self.serial
        valid = False
        tries = 0
        response = ''
        while not valid and tries < 60:
            write_serial(ser, hvac_code)
            response = read_serial(ser)
            tries += 1
            if isinstance(response, int):
                valid = True
        debug("tried %d times", tries)
        if response == hvac_code:
            debug("received mode number %d confirmation", hvac_code)
        else:
            error("got wrong confirmation from hvac:"
                  "expected %s, got %s" % (hvac_code, response))

    def fetch_hvac_state(self):
        ser = self.serial
        write_serial(ser, 10)
        json_obj = read_serial(ser)

        if json_obj:
            hvac_code = json_obj['mode']
            status = json_obj['status']
            if hvac_code in range(0, 4):
                self.set_hvac_code(hvac_code)
            else:
                warning("invalid mode number received: %s", hvac_code)
                return
            self.status = status
        else:
            warning("no json to parse")
            return

    def on_mqtt(self, client, userdata, message):
        message_string = str(message.payload.decode("utf-8"))
        debug("message received %s", message_string)
        debug("message topic=%s", message.topic)
        debug("message qos=%d", message.qos)
        debug("message retain flag=%d", message.retain)

        if message.topic == ('hvac/call/aux'):
            self.aux = aux_string(message_string)
        elif message.topic == ('hvac/call/mode'):
            self.mode = mode_string(message_string)

    def set_hvac_code(self, value):
        if value == 3:
            self.aux = 1
            self.mode = 2
        elif value == 2:
            self.aux = 0
            self.mode = 2
        else:
            self.mode = value


def aux_string(input):
    _aux_strings = ['OFF', 'ON']
    if isinstance(input, int):
        return _aux_strings[input]
    elif input in _aux_strings:
        return _aux_strings.index(input)
    else:
        error("Can not convert input %s", input)


def mode_string(input):
    _mode_strings = ['off', 'fan_only', 'heat']
    if isinstance(input, int):
        return _mode_strings[input]
    elif input in _mode_strings:
        return _mode_strings.index(input)
    else:
        error("Can not convert input %s", input)


def loop(client, hvac_state):
    logging.basicConfig(level=logging.INFO)

    hvac_state.fetch_hvac_state()
    aux = hvac_state.aux
    mode = hvac_state.mode
    hvac_code = hvac_state.hvac_code
    status = hvac_state.status

    while True:
        client.loop(1)
        if aux != hvac_state.aux or mode != hvac_state.mode:
            if hvac_code != hvac_state.hvac_code:
                hvac_state.change_hvac_state(hvac_state.hvac_code)
            aux = hvac_state.aux
            mode = hvac_state.mode
            hvac_code = hvac_state.hvac_code
            debug("Publishing message to topic")
            client.publish("hvac/state/aux", aux_string(hvac_state.aux))
            client.publish("hvac/state/mode", mode_string(hvac_state.mode))
            client.publish("hvac/state/status",
                           json.dumps({'status': hvac_state.status}))
        else:
            hvac_state.fetch_hvac_state()
            if status != hvac_state.status:
                status = hvac_state.status
                client.publish("hvac/state/status",
                               json.dumps({'status': status}))


def main(port, broker_address):
    hvac_state = HvacState()
    client = mqtt.Client("HVAC")  # create new instance

    # Start serial connection
    hvac_state.serial = setup_serial(port, 9600, 1)

    info("creating new instance")
    client.on_message = hvac_state.on_mqtt  # attach function to callback

    info("connecting to broker %s", broker_address)
    client.connect(broker_address)  # connect to broker

    info("Subscribing to topics")
    client.subscribe("hvac/call/mode")
    client.subscribe("hvac/call/aux")

    loop(client, hvac_state)


if __name__ == "__main__":
    main('/dev/ttyAMA0', 'localhost')
