# Performance synchroner und asynchroner I/O in Netzwerkanwendungen

_Dieses Repository enthält die Quelltexte für die Beispiele aus meinem Vortrag auf der parallel-Konferenz 2015 in Karlsruhe._

## Abstract

Die Nebenläufigkeit in skalierbaren Netzwerkanwendungen lässt sich mit sowohl synchroner als auch mit asynchroner I/O realisieren. Der Vortrag stellt die maßgeblichen Performance-Aspekte dieser beiden konkurrierenden Paradigmen gegenüber. Dazu werden zunächst die grundlegenden Unterschiede und Gemeinsamkeiten anhand eines Beispiels erörtert und zwei konkrete Implementierungen in C++ und Boost-Asio bezüglich Durchsatz, Latenz, Skalierbarkeit und Ressourcenverbrauch miteinander verglichen. Anschließend werden typische Anforderungen und Erweiterungen behandelt, die in der Praxis zu einer wesentlichen Verschiebung der Ergebnisse führen und entscheidend für die Abwägung zwischen den beiden Paradigmen sein können.

## Skills

Teilnehmer sollten Kenntnisse der synchronen oder asynchronen Netzwerkprogrammierung auf mindestens einer Plattform mitbringen. Die Beispiele in C++ sind so einfach gehalten, dass sie sich einfach auf andere Plattformen übertragen lassen. Darüber hinaus sollten die Teilnehmer ein Interesse an hochskalierenden und massiv nebenläufigen Netzwerkdiensten haben.

## Lernziele

Der Vortrag vermittelt Softwareentwicklern und -architekten an einem konkreten Beispiel belastbare Vor- und Nachteile synchroner und asynchroner I/O für massiv nebenläufige Netzwerkdienste. Damit wird ihnen eine Hilfestellung an die Hand gegeben, um in entsprechenden Projekten eine fundierte Architekturentscheidung treffen zu können. Für zukünftige Projekte sollten sich insbesondere die folgenden Fragen beantworten lassen: Gibt es klare Performance-Vorteile für synchrone I/O, für asynchrone I/O oder sind die Unterschiede in diesem Anwendungsfall vernachlässigbar?

## Stichwörter

Nebenläufigkeit, Performance, synchrone I/O, asynchrone I/O, C10K, boost-asio, Einordnung
