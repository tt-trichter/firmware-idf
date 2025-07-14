# Upload Image

- Host: http://4.231.40.213
- Path: /api/v1/images
- Method: POST
- Headers:
  - Authorization: Basic dHJpY2h0ZXI6c3VwZXItc2FmZS1wYXNzd29yZA==
  - Content-Type: image/jpeg
  - Accepts: text/plain
- Body: JPEG image in bytes

## Returns

Image resource name as plain text.

# Create run

- Host: http://4.231.40.213
- Path: /api/v1/runs
- Method: POST
- Headers:
  - Authorization: Basic dHJpY2h0ZXI6c3VwZXItc2FmZS1wYXNzd29yZA==
  - Content-Type: application/json
- Body:

```json
{
  "rate": 0.0, //rate in L/min as float
  "duration": 0.0, //duration in seconds as float
  "volume": 0.0, //volume in liters as float
  "image": "placeholder" //image resource name returned by upload image
}
```