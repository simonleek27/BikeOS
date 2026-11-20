import CoreLocation
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

    func testPausedStatusCarriesAuthoritativeRunStateAndZeroSpeed() {
        var payload = Data([1, BikeOSNavigationState.navigating.rawValue, BikeOSGpsSource.simulated.rawValue, 3])
        payload.appendLE(Int32(523548000)); payload.appendLE(Int32(48775000)); payload.appendLE(UInt32(42)); payload.appendLE(UInt32(120)); payload.appendLE(UInt32(18)); payload.appendLE(UInt32(999)); payload.appendLE(UInt16(12345)); payload.appendLE(UInt16(0)); payload.appendLE(UInt16(17)); payload.appendLE(UInt16(2))
        let status = BikeOSStatus.decode(BikeOSFrame(type: .status, transferID: 0, sequence: 1, flags: 0, payload: payload))
        XCTAssertEqual(status?.runState, .paused)
        XCTAssertEqual(status?.speedMPS ?? -1, 0, accuracy: 0.001)
    }

    func testRouteEncoderRoundTripsSupportedGeometry() throws {
        let origin = CLLocationCoordinate2D(latitude: 52.3548, longitude: 4.8775)
        let route = BikeOSRoutePlan(id: 42,
                                    name: "Test route",
                                    points: [origin,
                                             CLLocationCoordinate2D(latitude: 52.3550, longitude: 4.8777),
                                             CLLocationCoordinate2D(latitude: 52.3552, longitude: 4.8777)],
                                    maneuvers: [BikeOSManeuver(id: 0, pointIndex: 1, type: .right, streetName: "Damrak", distanceM: 0)],
                                    source: "Test")
        let encoded = try BikeOSRouteEncoder.encode(route)
        let decoded = try BikeOSRouteDecoder.decode(encoded)
        XCTAssertEqual(decoded.routeID, route.routeID)
        XCTAssertEqual(decoded.points.count, route.points.count)
        XCTAssertEqual(decoded.maneuvers.first?.streetName, "Damrak")
    }

    func testRouteEncoderRejectsInvalidOrTooWideGeometry() {
        let invalid = BikeOSRoutePlan(id: 3, name: "Invalid", points: [
            CLLocationCoordinate2D(latitude: 100.0, longitude: 4.0),
            CLLocationCoordinate2D(latitude: 52.0, longitude: 4.1)
        ])
        XCTAssertThrowsError(try BikeOSRouteEncoder.encode(invalid))

        let tooWide = BikeOSRoutePlan(id: 4, name: "Too wide", points: [
            CLLocationCoordinate2D(latitude: 52.0, longitude: 4.0),
            CLLocationCoordinate2D(latitude: 52.0, longitude: 4.05)
        ])
        XCTAssertThrowsError(try BikeOSRouteEncoder.encode(tooWide))
    }

    func testBLEFragmentEnvelopeAtLowAndHighATTLimits() {
        let frame = BikeOSFrame(type: .transferChunk,
                                transferID: 17,
                                sequence: 4,
                                flags: 0,
                                payload: Data(repeating: 0xA5, count: 180)).encoded()
        for limit in [20, 23, 244, 247] {
            if frame.count <= limit { continue }
            let fragments = BikeOSBLEFragment.makeFragments(frame: frame, messageID: 9, maxWriteLength: limit)
            XCTAssertNotNil(fragments)
            XCTAssertTrue(fragments!.allSatisfy { $0.count <= limit })
            XCTAssertEqual(fragments!.first?[0], BikeOSBLEFragment.magic)
            var joined = Data()
            for fragment in fragments! {
                XCTAssertEqual(fragment[1], BikeOSBLEFragment.version)
                joined.append(fragment.dropFirst(BikeOSBLEFragment.headerSize))
            }
            XCTAssertEqual(joined, frame)
            XCTAssertNotNil(BikeOSFrame.decode(joined))
        }
    }
}
