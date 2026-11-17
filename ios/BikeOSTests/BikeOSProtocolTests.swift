import XCTest
@testable import BikeOS

final class BikeOSProtocolTests: XCTestCase {
    func testFrameRoundTripAndCRC() {
        let frame = BikeOSFrame(type: .statusRequest, transferID: 7, sequence: 3, flags: 0, payload: Data([1, 2, 3])).encoded()
        let decoded = BikeOSFrame.decode(frame)
        XCTAssertEqual(decoded?.type, .statusRequest); XCTAssertEqual(decoded?.transferID, 7); XCTAssertEqual(decoded?.payload, Data([1, 2, 3]))
        var corrupt = frame; corrupt[20] ^= 0xFF; XCTAssertNil(BikeOSFrame.decode(corrupt))
    }
}
