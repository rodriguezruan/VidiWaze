function doPost(e) {
  try {
    //var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
    var sheet = SpreadsheetApp.getActiveSpreadsheet().getSheetByName("esp32");
    var payload = JSON.parse(e.postData.contents);
    
    // Check if the incoming data is an array of rows
    if (Array.isArray(payload.data)) {
      for (var i = 0; i < payload.data.length; i++) {
        sheet.appendRow(payload.data[i]);
      }
      return ContentService.createTextOutput("Success: Batch Processed");
    } else {
      return ContentService.createTextOutput("Error: Invalid Data Format");
    }
  } catch(error) {
    return ContentService.createTextOutput("Error: " + error.toString());
  }
}
