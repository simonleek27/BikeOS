import XCTest
@testable import BikeOS

final class BikeOSProtocolTests: XCTestCase {
    func testFrameRoundTripAndCRC() {
        let frame = BikeOSFrame(type: .statusRequest, transferID: 7, sequence: 3, flags: 0, payload: Data([1, 2, 3])).encoded()
        let decoded = BikeOSFrame.decode(frame)
        XCTAssertEqual(decoded?.type, .statusRequest); XCTAssertEqual(decoded?.transferID, 7); XCTAssertEqual(decoded?.payload, Data([1, 2, 3]))
        var corrupt = frame; corrupt[20] ^= 0xFF; XCTAssertNil(BikeOSFrame.decode(corrupt))
    }

    func testBRTERouteDecoderReadsPointsAndManeuver() throws {
        var payload = Data()
        payload.appendLE(Int16(0)); payload.appendLE(Int16(0))
        payload.appendLE(Int16(100)); payload.appendLE(Int16(200))
        payload.appendLE(UInt16(1)); payload.append(UInt8(BikeOSManeuverType.right.rawValue)); payload.append(UInt8(6)); payload.append(contentsOf: Data("Damrak".utf8))
        var encoded = Data([0x42, 0x52, 0x54, 0x45])
        encoded.appendLE(UInt16(1)); encoded.appendLE(UInt16(0)); encoded.appendLE(UInt32(42))
        encoded.appendLE(Int32(52_354_8000)); encoded.appendLE(Int32(4_877_5000))
        encoded.appendLE(UInt16(2)); encoded.appendLE(UInt16(1)); encoded.appendLE(UInt32(payload.count)); encoded.appendLE(BikeOSFrame.crc32(payload)); encoded.append(payload)

        let route = try BikeOSRouteDecoder.decode(encoded)
        XCTAssertEqual(route.routeID, 42)
        XCTAssertEqual(route.points.count, 2)
        XCTAssertEqual(route.maneuvers.first?.type, .right)
        XCTAssertEqual(route.maneuvers.first?.streetName, "Damrak")
        XCTAssertGreaterThan(route.distanceM, 0)
    }

    func testBRTERouteDecoderRejectsCorruptPayload() {
        XCTAssertThrowsError(try BikeOSRouteDecoder.decode(Data("not a route".utf8)))
    }

    func testExtendedStatusAndAcknowledgementDecode() {
        var statusPayload = Data([1, BikeOSNavigationState.navigating.rawValue, BikeOSGpsSource.simulated.rawValue, 3])
        statusPayload.appendLE(Int32(523548000)); statusPayload.appendLE(Int32(48775000)); statusPayload.appendLE(UInt32(42)); statusPayload.appendLE(UInt32(120)); statusPayload.appendLE(UInt32(18)); statusPayload.appendLE(UInt32(999)); statusPayload.appendLE(UInt16(12345)); statusPayload.appendLE(UInt16(450)); statusPayload.appendLE(UInt16(17)); statusPayload.appendLE(UInt16(0))
        let statusFrame = BikeOSFrame(type: .status, transferID: 0, sequence: 1, flags: 0, payload: statusPayload)
        let status = BikeOSStatus.decode(statusFrame)
        XCTAssertEqual(status?.headingDeg ?? -1, 123.45, accuracy: 0.001)
        XCTAssertEqual(status?.speedMPS ?? -1, 4.5, accuracy: 0.001)
        XCTAssertEqual(status?.matchedPointIndex, 17)

        var ackPayload = Data([BikeOSResultCode.accepted.rawValue]); ackPayload.appendLE(UInt32(150)); ackPayload.appendLE(UInt32(99))
        let ack = BikeOSAcknowledgement.decode(BikeOSFrame(type: .acknowledgement, transferID: 8, sequence: 1, flags: 0, payload: ackPayload))
        XCTAssertEqual(ack?.result, .accepted); XCTAssertEqual(ack?.nextOffset, 150); XCTAssertEqual(ack?.bundleID, 99)
    }
}
