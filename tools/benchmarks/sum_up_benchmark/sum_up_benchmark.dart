import 'dart:convert';
import 'dart:io';

void main() {
  // Determine the directory of the executable
  String executableDir = File(Platform.resolvedExecutable).parent.path;

  // Construct the paths relative to the executable
  const String jsonFileName = 'huge_random_numbers_with_strings.json';
  const String resultFileName = '.tmp-automatic-created-result';

  String jsonFilePath = '$executableDir/$jsonFileName';
  String resultFilePath = '$executableDir/$resultFileName';

  // Get the current timestamp
  final startTime = DateTime.now();

  try {
    while (true) {
      for (int i = 1; i <= 100; i++) {
        String key = 'numbers${i.toString().padLeft(3, '0')}';

        // Check if 10 seconds have passed
        final currentTime = DateTime.now();
        if (currentTime.difference(startTime).inSeconds >= 10) {
          print('Time limit reached, exiting...');
          return;
        }

        // Delete the result file if it exists at the start of each iteration
        File resultFile = File(resultFilePath);
        if (resultFile.existsSync()) {
          resultFile.deleteSync();
        }

        // Load the JSON file on every iteration
        String jsonString = File(jsonFilePath).readAsStringSync();
        Map<String, dynamic> jsonData = jsonDecode(jsonString);

        if (jsonData.containsKey(key)) {
          List<dynamic> numbersList = jsonData[key];

          num sum = 0;
          for (var item in numbersList) {
            if (item is String) {
              sum += num.tryParse(item) ?? 0; // Convert string to number safely
            } else if (item is num) {
              sum += item; // Directly add if it's already a number
            }
          }

          // Write to a new file (overwriting it each iteration)
          resultFile.writeAsStringSync('$key sum: $sum\n');

          print('$key sum: $sum');
        } else {
          resultFile.writeAsStringSync('$key not found in JSON\n');
        }

        print('Processed $key and wrote result to $resultFilePath');
      }
    }
  } finally {
    // Ensure the result file is deleted when the program exits
    File resultFile = File(resultFilePath);
    if (resultFile.existsSync()) {
      resultFile.deleteSync();
      print('Deleted result file: $resultFilePath');
    }
  }
}
