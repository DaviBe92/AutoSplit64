import onnxruntime as ort
import numpy as np
import logging

from .image_utils import convert_to_np

class PredictionInfo(object):
    def __init__(self, prediction, probability):
        self.prediction = prediction
        self.probability = probability


class Model(object):
    def __init__(self, model_path, width, height):
        try:
            # Configure ONNX Runtime session options
            session_options = ort.SessionOptions()
            session_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
            session_options.intra_op_num_threads = 4  # Adjust based on CPU cores
            session_options.inter_op_num_threads = 1
            session_options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
            
            # Set execution providers - CPU optimized
            providers = [
                ('CPUExecutionProvider', {
                    'arena_extend_strategy': 'kSameAsRequested',
                    'cpu_memory_arena_cfg': [1024, 1024],
                })
            ]
            
            self.model = ort.InferenceSession(
                model_path,
                sess_options=session_options,
                providers=providers
            )
            self.input_name = self.model.get_inputs()[0].name
            self.output_name = self.model.get_outputs()[0].name
            
            # Enable memory pattern optimization
            self.model.disable_fallback()
            
        except Exception as e:
            self.model = None
            logging.error(f"Failed to load model: {str(e)}")

        self.width = int(width)
        self.height = int(height)
    
    def valid(self):
        if self.model:
            return True
        else:
            return False

    def predict(self, image) -> PredictionInfo:
        try:
            # Preprocess image
            np_img = convert_to_np(image)
                
            # Run prediction
            model_output = self.model.run([self.output_name], {self.input_name: np_img})[0]
            prediction = np.argmax(model_output)
            probability = np.max(model_output)

            return PredictionInfo(prediction, probability)
        except Exception as e:
            logging.error(f"Prediction failed: {str(e)}")
            return PredictionInfo(0, 0.0)