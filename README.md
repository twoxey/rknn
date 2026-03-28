# Yolo11 image recoginition on Orange pi 5 using NPU

Test project for using the NPU to run the Yolo11 model on rk5388.

The repository is adapted from the yolo11 example from [rknn_model_zoo](https://github.com/airockchip/rknn_model_zoo).

The `yolo11.rknn` file is converted from the [./yolo11n.onnx](https://ftrg.zbox.filez.com/v2/delivery/data/95f00b0fc900458ba134f8b180b3f7a1/examples/yolo11/yolo11n.onnx) file from [the example](https://github.com/airockchip/rknn_model_zoo/tree/main/examples/yolo11).

**Important:**

- The `rknn-toolkit2` python library required for converting the model only supports python3.12 on linux, which requires Ubuntu. See the `Notes` section of <https://pypi.org/project/rknn-toolkit2/>. WSL Ubuntu can be used.

- When running `pip install rknn-toolikt2`, it's dependency `setuptools` removed the `pkg_resources` library in verion 82.0.0[^1], which is required by other dependencies. And when converting the model, the `onnx` module will error for newer version[^2], so install the with the following command instead:

  `pip install rknn-toolikt2 setuptools==81.0.0 onnx==1.18.0`

[^1]: https://setuptools.pypa.io/en/stable/history.html#v82-0-0
[^2]: https://github.com/airockchip/rknn_model_zoo/issues/388

