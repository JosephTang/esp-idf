import re
import os

import ttfw_idf


@ttfw_idf.idf_example_test(env_tag="Example_WIFI", ignore=True)
def test_examples_protocol_esp_http_client(env, extra_data):
    """
    steps: |
      1. join AP
      2. Send HTTP request to httpbin.org
    """
    dut1 = env.get_dut("esp_http_client", "examples/protocols/esp_http_client")
    # check and log bin size
    binary_file = os.path.join(dut1.app.binary_path, "esp-http-client-example.bin")
    bin_size = os.path.getsize(binary_file)
    ttfw_idf.log_performance("esp_http_client_bin_size", "{}KB".format(bin_size // 1024))
    ttfw_idf.check_performance("esp_http_client_bin_size", bin_size // 1024)
    # start test
    dut1.start_app()
    dut1.expect("Connected to AP, begin http example", timeout=30)
    dut1.expect(re.compile(r"HTTP GET Status = 200, content_length = (\d)"))
    dut1.expect(re.compile(r"HTTP POST Status = 200, content_length = (\d)"))
    dut1.expect(re.compile(r"HTTP PUT Status = 200, content_length = (\d)"))
    dut1.expect(re.compile(r"HTTP PATCH Status = 200, content_length = (\d)"))
    dut1.expect(re.compile(r"HTTP DELETE Status = 200, content_length = (\d)"))
    dut1.expect(re.compile(r"HTTP HEAD Status = 200, content_length = (\d)"))
    dut1.expect(re.compile(r"HTTP Basic Auth Status = 200, content_length = (\d)"))
    dut1.expect(re.compile(r"HTTP Basic Auth redirect Status = 200, content_length = (\d)"))
    dut1.expect(re.compile(r"HTTP Digest Auth Status = 200, content_length = (\d)"))
    dut1.expect(re.compile(r"HTTP Relative path redirect Status = 200, content_length = (\d)"))
    dut1.expect(re.compile(r"HTTP Absolute path redirect Status = 200, content_length = (\d)"))
    dut1.expect(re.compile(r"HTTPS Status = 200, content_length = (\d)"))
    dut1.expect(re.compile(r"HTTPS Status = 200, content_length = (\d)"))
    dut1.expect(re.compile(r"HTTP redirect to HTTPS Status = 200, content_length = (\d)"), timeout=10)
    dut1.expect(re.compile(r"HTTP chunk encoding Status = 200, content_length = (\d)"))
    dut1.expect(re.compile(r"HTTP Stream reader Status = 200, content_length = (\d)"))
    dut1.expect(re.compile(r"Last esp error code: 0x8001"))
    dut1.expect("Finish http example")


if __name__ == '__main__':
    test_examples_protocol_esp_http_client()
